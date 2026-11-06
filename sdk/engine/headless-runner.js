'use strict';
const fs=require('node:fs'),path=require('node:path');
const native=require('./working-database');
const {planChangedFiles,digest,targetKey,fileKey}=require('./publish-changes');
const {parseSource}=require('./source_symbols');
const {testDependencies}=require('./test-dependencies');
const {scanProject,indexes}=require('./native-project');
const {runProject}=require('./project-runner');

// This state describes the disposable build base, never the user's publish target.
function startWorking(config,operation,onOutput=()=>{},onReport=()=>{},dependencies={}){
 const backend={...native,...dependencies},abort=new AbortController();let cancelled=false;
 const completion=(async()=>{
  let working;
  try{
   working=await backend.prepareWorkingDatabase({...config,signal:abort.signal,timeoutMilliseconds:config.timeoutMilliseconds||(config.testTimeoutSeconds||120)*1000},onOutput);
   if(config.beforeNativeProcess)working.beforeNativeProcess=config.beforeNativeProcess;
   if(config.testSession)working.testSession=config.testSession;
   onOutput('Using isolated working database '+working.databasePath+'\nConfigured publish target: '+config.databasePath+'\n');
   if(abort.signal.aborted)throw new Error('Operation cancelled.');
   const stateFile=path.join(working.root,'loaded-files.json');let state={};
   try{state=JSON.parse(fs.readFileSync(stateFile,'utf8'));}catch(error){if(error.code!=='ENOENT')throw error;}
   const workspaceFiles=config.files||[config.program],root=config.workspaceRoots?.[0]||path.dirname(config.program);
   const project=await scanProject(root);
   const resources=project.schemas.flatMap(s=>[s.header,...s.entries.filter(e=>e.extension!=='.cls'&&!indexes.includes(path.basename(e.file,'.sec')))].map(e=>({...e,schema:s.name,parentSchema:s.parent})));
   const files=operation==='test.run'?await testDependencies(workspaceFiles,config.program,resources,{project}):workspaceFiles;
   if(!project.schemas.length&&project.migrations.length)throw new Error('Pull or create schema headers before applying tracked native migrations.');
   if(project.schemas.length)return await runProject(config,working,project,operation,files,onOutput,onReport);
   const settings={...config,databasePath:working.databasePath,iniPath:working.iniPath,server:'singleUser'};
   const plan=await (backend.plan||planChangedFiles)(files,root,settings,state);
   const removed=Object.keys(state[targetKey(settings)]||{}).filter(key=>!workspaceFiles.some(file=>key.startsWith(path.resolve(file).toLowerCase()+'::')));
   if(removed.length)throw new Error('Previously loaded source files were removed or renamed. Recreate the working database to remove stale definitions before compiling.');
   if(operation==='test.run')onOutput('Selected test and '+Math.max(0,files.length-1)+' workspace dependency file(s); unrelated files are not loaded.\n');
   onOutput('Synchronising '+plan.length+' changed CLS file(s) into the working database.\n');
   working.expectedHashes=Object.fromEntries(plan.map(row=>[row.source,row.hash]));
   if(plan.length){
    // A native MUL can commit earlier files before a later error. Retain source
    // identities for deletion checks, but no successful hashes across mutation.
    const target=targetKey(settings);state[target]||={};
    for(const [key,value] of Object.entries(state[target]))state[target][key]={...value,hash:null,pending:true};
    for(const entry of plan){const key=targetKey(entry.config||settings),sourceKey=fileKey(entry.config||{...settings,source:entry.source,schema:config.schema});state[key]||={};state[key][sourceKey]={...state[key][sourceKey],hash:null,pending:true};}
    fs.writeFileSync(stateFile,JSON.stringify(state,null,2)+'\n');
   }
   const loaded=plan.length?await backend.loadWorkingFiles(working,plan.map(row=>row.source),onOutput):{success:true,code:0,results:[]};
   const diagnostics=loaded.results.flatMap(row=>row.diagnostics||[]);
   const report={source:config.program,compiled:loaded.success,success:loaded.success,diagnostics,loaded_files:loaded.results.length,workingDatabase:working.databasePath,databaseNeverOpened:false};
   if(!loaded.success){report.error=loaded.cancelled?'Operation cancelled.':loaded.timedOut?'Native loader timed out.':'Native schema load failed (exit '+loaded.code+'). See Jade Compile output.';onReport({...report,compiled:false});return {code:loaded.code,passed:false,cancelled:loaded.cancelled||cancelled,timedOut:loaded.timedOut,report};}
   for(const entry of plan){const key=targetKey(entry.config||settings);state[key]||={};state[key][fileKey(entry.config||{...settings,source:entry.source,schema:config.schema})]={hash:entry.hash||digest(fs.readFileSync(entry.source)),date:new Date().toISOString()};}
   fs.writeFileSync(stateFile,JSON.stringify(state,null,2)+'\n');
   if(abort.signal.aborted)throw new Error('Operation cancelled.');
   if(operation==='compile'){onReport(report);return {code:0,passed:true,report,cancelled:false,timedOut:false};}
   if(operation!=='test.run')throw new Error('Unsupported working-database operation: '+operation);
   const text=fs.readFileSync(config.program,'utf8'),model=parseSource(text,config.program),owner=model.classes[0]?.name;
   if(!owner)throw new Error('Select a CLS containing a test class.');
   const methods=config.methods?(Array.isArray(config.methods)?config.methods:[config.methods]):undefined;
   const tested=await backend.runNativeTests(working,{schema:model.schema,owner,source:config.program,text,methods},onOutput);
   Object.assign(report,tested,{compiled:true});
   for(const result of report.results||[])onOutput(result.status.toUpperCase()+': '+result.owner+'::'+result.method+(result.message?' - '+result.message:'')+'\n');
   onReport(report);return {code:tested.code,passed:tested.success,cancelled:tested.cancelled||cancelled,timedOut:tested.timedOut,report};
  }catch(error){const report={source:config.program,compiled:false,success:false,error:error.message,workingDatabase:working?.databasePath};return {code:2,passed:false,cancelled:cancelled||abort.signal.aborted,report};}
 })();
 return {completion,stop:()=>{cancelled=true;abort.abort();}};
}
module.exports={startWorking};
