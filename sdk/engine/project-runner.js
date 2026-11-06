'use strict';
const fs=require('node:fs'),path=require('node:path');
const {hash}=require('./native-project');
const {joinProject,loadProject}=require('./native-project-build');
const {targetKey,fileKey}=require('./publish-changes');
const {parseSource}=require('./source_symbols');
const {nativeProcess,runNativeTests,loadWorkingFiles}=require('./working-database');
const {snapshotProject}=require('./incremental-project');
const {snapshotInputs,inventoryPlan}=require('./native-source-inventory');
const {applyRenames}=require('./native-renames');
function dynamicDependencies(project,files){
 const selected=new Set(files);return project.schemas.some(s=>s.classes.some(c=>selected.has(c.file))&&(s.entries.some(e=>/(?:imported|exported)PackageDefinitions\.sec$/i.test(e.file)&&e.text.replace(/\b(?:imported|exported)PackageDefinitions\b/g,'').trim())||s.classes.some(c=>selected.has(c.file)&&/\b(?:getClass\w*|createInstance|createObject|createTransientInstance|createPersistentInstance)\s*\(|\bcreate\b[^;\r\n]*\bas\b/.test(c.model.code))));
}
function subsetProject(project,files){
 const selected=new Set(files),names=new Set();
 for(const s of project.schemas)if(s.classes.some(c=>selected.has(c.file)))names.add(s.name);
 let changed=true;while(changed){changed=false;for(const s of project.schemas)if(names.has(s.name)&&project.schemas.some(p=>p.name===s.parent)&&!names.has(s.parent)){names.add(s.parent);changed=true;}}
 const schemas=project.schemas.filter(s=>names.has(s.name)).map(s=>({...s,classes:s.classes.filter(c=>selected.has(c.file)),entries:s.entries.filter(e=>e.extension!=='.cls'||selected.has(e.file))}));
 const entries=schemas.flatMap(s=>[s.header,...s.entries]);return {...project,schemas,entries,hash:hash(Buffer.from(JSON.stringify(entries.map(e=>[e.file,e.hash]).sort())))};
}
async function runProject(config,working,project,operation,scopeFiles,output=()=>{},onReport=()=>{}){
 const stateFile=path.join(working.root,'native-project-state.json');let state={formatVersion:3,scopes:{},schemas:[]},legacyState=false;
 try{state=JSON.parse(fs.readFileSync(stateFile,'utf8'));legacyState=state.formatVersion!==3;}catch(error){if(error.code!=='ENOENT')throw error;}
 // Legacy cache claims may already describe a partially changed database.
 if(legacyState){state.scopes={};delete state.fullSnapshot;delete state.scopeSnapshot;delete state.nativeInputs;state.formatVersion=3;}
 const currentClasses=new Set(project.schemas.flatMap(s=>s.classes.map(c=>s.name+'::'+c.owner)));
 const removed=(state.classes||[]).some(name=>!currentClasses.has(name))||(state.schemas||[]).some(name=>!project.schemas.some(schema=>schema.name===name));
 let full=operation==='compile'||removed||project.migrations?.length||config.testDependencyScope==='workspace'||config.testDependencyScope!=='dependencies'&&dynamicDependencies(project,scopeFiles);
 // A verified full build already contains the unchanged test dependencies.
 full=full||Boolean(state.scopes?.full&&state.scopes.full===project.hash);
 const selected=full?project:subsetProject(project,scopeFiles),key=full?'full':config.program;
 const selectedSnapshot=snapshotProject(selected),selectedInputs=snapshotInputs(selected,selectedSnapshot),previousInputs=state.nativeInputs||{};
 const installed=inventoryPlan(selected,previousInputs,selectedInputs);
 state.scopes||={};const changed=removed||!installed.covered;
 output(full?'Using the complete native workspace, including dynamically selected types.\n':'Using the selected test and its schema-scoped native resources.\n');
 const report={source:config.program,compiled:true,success:true,workingDatabase:working.databasePath,databaseNeverOpened:false,native_project:true,loaded_files:0,diagnostics:[]};let bundle;
 if(changed){
  // Compare source against actual successful native loads, independently of test selection.
  const delta=removed?undefined:installed.delta;let loaded;
  // Native multi-file loads may commit before a later file fails. Invalidate all
  // claims about this shared database before the first possible native mutation.
  state.scopes={};delete state.fullSnapshot;delete state.scopeSnapshot;state.nativeInputs={};
  fs.writeFileSync(stateFile,JSON.stringify(state,null,2)+'\n');
  const loadedState={},settings={...config,...working,server:'singleUser'},target=targetKey(settings);
  try{Object.assign(loadedState,JSON.parse(fs.readFileSync(path.join(working.root,'loaded-files.json'),'utf8')));}catch(error){if(error.code!=='ENOENT')throw error;}
  loadedState[target]||={};
  for(const [key,value] of Object.entries(loadedState[target]))loadedState[target][key]={...value,hash:null,pending:true};
  for(const schema of selected.schemas)for(const cls of schema.classes){const key=fileKey({source:cls.file,schema:schema.name});loadedState[target][key]={...loadedState[target][key],hash:null,pending:true};}
  fs.writeFileSync(path.join(working.root,'loaded-files.json'),JSON.stringify(loadedState,null,2)+'\n');
  if(delta){
   output('Native declarations are unchanged. Compiling '+delta.length+' edited class file(s) with the native CLS loader.\n');
   const expectedHashes=Object.fromEntries(delta.map(entry=>[entry.file,entry.hash]));
   loaded=await loadWorkingFiles({...working,expectedHashes},delta.map(entry=>entry.file),output);
   loaded.diagnostics=(loaded.results||[]).flatMap(result=>result.diagnostics||[]);
   report.loaded_files=delta.length;report.incremental=true;
  }else{
  await applyRenames(working,project,output);
  bundle=await joinProject(working,selected,output);
  if(full){
   for(const name of [...(state.schemas||[])].reverse().filter(name=>!project.schemas.some(s=>s.name===name))){
    output('Removing deleted workspace schema '+name+' from the working database ...\n');
    const removed=await nativeProcess(path.join(working.installPath,'bin/jadloadb.exe'),['path='+working.databasePath,'ini='+working.iniPath,'server=singleUser','deleteSchema='+name],{...working,cwd:bundle.directory},output);
    if(!removed.success)throw Error('Native schema deletion failed: '+name+' (exit '+removed.code+').');
   }
  }
  loaded=await loadProject(working,bundle,output,{deleteIfAbsent:full});
  report.loaded_files=selected.entries.length;
  }
  report.diagnostics=loaded.diagnostics;
  if(!loaded.success){Object.assign(report,{compiled:false,success:false,error:'Native project load failed (exit '+loaded.code+'). See native output.'});onReport(report);return {code:loaded.code,passed:false,report,cancelled:loaded.cancelled,timedOut:loaded.timedOut};}
  state.scopes[key]=selected.hash;
  state.schemas=[...new Set([...(state.schemas||[]),...selected.schemas.map(s=>s.name)])];
  state.classes=[...new Set([...(state.classes||[]),...selected.schemas.flatMap(s=>s.classes.map(c=>s.name+'::'+c.owner))])];
  if(full){state.schemas=project.schemas.map(s=>s.name);state.classes=[...currentClasses];state.scopes={full:selected.hash};state.fullSnapshot=selectedSnapshot;}else state.scopeSnapshot={scope:key,snapshot:selectedSnapshot};
  state.nativeInputs=full?selectedInputs:{...previousInputs,...selectedInputs};
  fs.writeFileSync(stateFile,JSON.stringify(state,null,2)+'\n');
  // A complete successful reorganisation can replace the older deletion history.
  if(full)loadedState[target]={};
  // Only successfully loaded classes can establish new CLS-only cache entries.
  for(const schema of selected.schemas)for(const cls of schema.classes)loadedState[target][fileKey({source:cls.file,schema:schema.name})]={hash:cls.hash,date:new Date().toISOString()};
  fs.writeFileSync(path.join(working.root,'loaded-files.json'),JSON.stringify(loadedState,null,2)+'\n');
 }else{report.native_source_reused=true;output(operation==='compile'?'Native workspace inputs are unchanged.\n':'Native source inputs are already loaded. Executing the selected tests again.\n');}
 if(operation==='compile'){onReport(report);return {code:0,passed:true,report,bundle};}
 const text=fs.readFileSync(config.program,'utf8'),model=parseSource(text,config.program),owner=model.classes[0]?.name;
 if(!owner)throw Error('Select a CLS containing a test class.');
 const methods=config.methods?(Array.isArray(config.methods)?config.methods:[config.methods]):undefined;
 const tested=await runNativeTests(working,{schema:model.schema,owner,source:config.program,text,methods},output);Object.assign(report,tested,{compiled:true});
 for(const row of report.results||[])output(row.status.toUpperCase()+': '+row.owner+'::'+row.method+(row.message?' - '+row.message:'')+'\n');
 onReport(report);return {code:tested.code,passed:tested.success,report,cancelled:tested.cancelled,timedOut:tested.timedOut};
}
module.exports={runProject,subsetProject,dynamicDependencies};
