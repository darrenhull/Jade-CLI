 'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),{spawn}=require('node:child_process');
const {StringDecoder}=require('node:string_decoder');
const {compilerDiagnostics,nativeTestReport}=require('./native-reports');
const {schemaFromSource}=require('./config');
// Resolve existing ancestors too, so junctions cannot redirect the working base
// back into the source workspace or configured publish target.
const canonical=value=>{let current=path.resolve(value),tail=[];while(!fs.existsSync(current)){const parent=path.dirname(current);if(parent===current)break;tail.unshift(path.basename(current));current=parent;}return path.join(fs.existsSync(current)?fs.realpathSync.native(current):current,...tail).toLowerCase();};
const contains=(root,value)=>{const relative=path.relative(canonical(root),canonical(value));return relative===''||relative!=='..'&&!relative.startsWith('..'+path.sep)&&!path.isAbsolute(relative);};
async function nativeProcess(executable,args,options={},output=()=>{}){
 if(options.testSession&&path.basename(executable).toLowerCase()==='jadloadb.exe')return options.testSession.load(options,args,output);
 await options.beforeNativeProcess?.();
 return new Promise((resolve,reject)=>{
  if(options.root)fs.appendFileSync(path.join(options.root,'native-commands.jsonl'),JSON.stringify({exe:executable,args,startedUtc:new Date().toISOString()})+'\n');
  const child=spawn(executable,args,{windowsHide:true,shell:false,cwd:options.cwd||path.dirname(executable)}),chunks=[];const decoders={stdout:new StringDecoder('utf8'),stderr:new StringDecoder('utf8')};let expired=false,cancelled=false;
  const timer=setTimeout(()=>{expired=true;child.kill();},options.timeoutMilliseconds||120000);const abort=()=>{cancelled=true;child.kill();};options.signal?.addEventListener('abort',abort,{once:true});if(options.signal?.aborted)abort();
  for(const stream of ['stdout','stderr'])child[stream].on('data',bytes=>{const text=decoders[stream].write(bytes);chunks.push(text);output(text);});
  child.once('error',error=>{clearTimeout(timer);options.signal?.removeEventListener('abort',abort);reject(error);});
  child.once('close',(code,signal)=>{clearTimeout(timer);options.signal?.removeEventListener('abort',abort);for(const decoder of Object.values(decoders)){const tail=decoder.end();chunks.push(tail);if(tail)output(tail);}resolve({code:code??2,signal,success:code===0&&!expired&&!cancelled,output:chunks.join(''),timedOut:expired,cancelled});});
 });
}
function isolatedIni(text,directories){
 const overrides={Jade:{Server:'SingleUser'},NonGuiClient:{Server:'SingleUser'},JadeLog:{LogDirectory:directories.logs},PersistentDb:{ActivityLogDirectory:directories.logs,JournalRootDirectory:directories.journals},TransientDb:{FileDirectory:directories.transient,ActivityLogDirectory:directories.logs},JadeEnvironment:{JadeWorkDirectory:directories.scratch,ProgramDataDirectory:directories.environment},FaultHandling:{CrashLogDirectory:directories.logs,ProcessDumpDirectory:directories.logs},JadeMonitorBackground:{DefaultLogDirectory:directories.logs}};
 const pending=new Map(Object.entries(overrides).map(([section,values])=>[section.toLowerCase(),{section,values:new Map(Object.entries(values).map(([key,value])=>[key.toLowerCase(),[key,value]]))}]));
 let current,lines=[];const finish=()=>{if(current){for(const [key,value] of current.values.values())lines.push(key+'='+value);pending.delete(current.section.toLowerCase());}};
 for(const line of text.split(/\r?\n/)){const section=/^\s*\[([^\]]+)\]/.exec(line);if(section){finish();current=pending.get(section[1].toLowerCase());lines.push(line);continue;}const key=/^\s*([^;#=]+?)\s*=/.exec(line),value=current&&key&&current.values.get(key[1].toLowerCase());if(value){lines.push(value[0]+'='+value[1]);current.values.delete(key[1].toLowerCase());}else lines.push(line);}
 finish();for(const row of pending.values()){lines.push('['+row.section+']');for(const [key,value] of row.values.values())lines.push(key+'='+value);}return lines.join('\r\n');
}
function workingLocation(config){
 const seed=path.resolve(config.databasePath),install=path.resolve(config.installPath),identity=crypto.createHash('sha256').update(JSON.stringify([canonical(seed),canonical(install),canonical(config.iniPath||path.join(seed,'jade.ini')),(config.workspaceRoots||[]).map(canonical),config.workingDatabaseGeneration||'initial'])).digest('hex').slice(0,24);
 const root=path.resolve(config.workingDatabaseDirectory||path.join(config.storagePath,'working-databases',identity));
 return {seed,install,identity,root,databasePath:path.join(root,'system')};
}
async function prepareWorkingDatabase(config,output=()=>{}){
 const {seed,install,identity,root}=workingLocation(config);
 const online=config.server==='multiUser',sourceIniPath=config.iniPath||path.join(seed,'jade.ini');
 if(contains(seed,root)||contains(root,seed)||(config.workspaceRoots||[]).some(workspace=>contains(workspace,root)||contains(root,workspace)))throw new Error('Working database must be separate from the configured target database and workspace source.');
 for(const file of ['jdbutilb.exe','jadloadb.exe','jadclient.exe',...(online?['jdbadmin.exe']:[])])if(!fs.existsSync(path.join(install,'bin',file)))throw new Error('Native Jade tool not found: '+file);
 if(!fs.existsSync(path.join(seed,'_control.dat')))throw new Error('Configured database has no _control.dat.');
 const databasePath=path.join(root,'system'),iniPath=path.join(root,'working.ini'),marker=path.join(root,'working-database.json');
 const result={root,databasePath,iniPath,installPath:install,sourceDatabasePath:seed,timeoutMilliseconds:config.timeoutMilliseconds,signal:config.signal,beforeNativeProcess:config.beforeNativeProcess};
 if(fs.existsSync(marker)){const saved=JSON.parse(fs.readFileSync(marker,'utf8'));if(saved.identity!==identity||!fs.existsSync(path.join(databasePath,'_control.dat')))throw new Error('Working database is incomplete or belongs to another target.');return result;}
 if(fs.existsSync(path.join(databasePath,'_control.dat')))throw new Error('Unmanaged working database exists; select a new empty working-database directory.');
 const directories=Object.fromEntries(['logs','journals','transient','scratch','environment','backup','system'].map(name=>[name,path.join(root,name)]));for(const directory of Object.values(directories))fs.mkdirSync(directory,{recursive:true});
 fs.writeFileSync(iniPath,isolatedIni(decodeNativeText(fs.readFileSync(sourceIniPath)),directories));
 const utility=path.join(install,'bin/jdbutilb.exe');
 output(online?'Creating a native online quiesced backup for the working database; source clients stay connected while updates temporarily pause ...\n':'Creating a consistent native backup for the working database ...\n');
 // Keep the original source connection INI for MultiUser. The generated INI
 // redirects writable locations and is only for the independent local restore.
 // Native tools can append default keys, so give them an unchanged byte-copy
 // of the source connection settings rather than the user's original file.
 const backupIniPath=path.join(root,'source-backup.ini');
 if(online)fs.copyFileSync(sourceIniPath,backupIniPath);
 const backupTool=online?path.join(install,'bin/jdbadmin.exe'):utility;
 const backupArgs=online?['action=Backup','path='+seed,'ini='+backupIniPath,'server=multiUser','backupDir='+directories.backup,'quiesced=true','verify=true','showProgress=true']:['path='+seed,'ini='+iniPath,'backup','backupDir='+directories.backup,'verify','nostatus'];
 const backup=await nativeProcess(backupTool,backupArgs,{...result,cwd:root},output);if(!backup.success)throw new Error('Native database backup failed (exit '+backup.code+'). '+backup.output);
 // Native quiesced online backups are commit-consistent, like offline backups;
 // noRecovery is correct here, but not for an updates-allowed online backup.
 output('Restoring the isolated working database ...\n');const restored=await nativeProcess(utility,['path='+databasePath,'ini='+iniPath,'restore','backupDir='+directories.backup,'noRecovery','clearFilePaths','verify','nostatus'],{...result,cwd:root},output);if(!restored.success)throw new Error('Native working database restore failed (exit '+restored.code+'). '+restored.output);
 if(!fs.existsSync(path.join(databasePath,'_control.dat')))throw new Error('Native restore did not create the working database.');
 fs.writeFileSync(marker,JSON.stringify({formatVersion:1,identity,sourceDatabasePath:seed,createdUtc:new Date().toISOString()},null,2)+'\n');return result;
}
async function loadWorkingFiles(working,files,output=()=>{}){
 if(!files.length)return {success:true,code:0,results:[],output:''};
 const directory=path.join(working.root,'loads',crypto.randomUUID());fs.mkdirSync(directory,{recursive:true});
 // Native MUL preserves source bytes and lets Jade handle compilation in one session.
 const staged=files.map((source,index)=>{const bytes=fs.readFileSync(source),text=decodeNativeText(bytes),schema=schemaFromSource(text);if(!schema)throw new Error('Source has no schema declaration: '+source);if(working.expectedHashes?.[source]&&crypto.createHash('sha256').update(bytes).digest('hex')!==working.expectedHashes[source])throw new Error('Source changed while preparing native load. Compile again: '+source);const file=path.join(directory,index+'.cls');fs.writeFileSync(file,bytes);return {source,text,schema,staged:file};});
 const batch=path.join(directory,'sources.mul');fs.writeFileSync(batch,'#MULTIPLE_SCHEMA_EXTRACT\n'+staged.map((row,index)=>index+'.cls').join('\n')+'\n');
 for(const row of staged)output('Loading and compiling '+row.source+' in the working database ...\n');
 // This entry point is also used by the CLS-only mode. Its mutations invalidate
 // any earlier complete-project baseline in the same disposable database.
 const projectStateFile=path.join(working.root,'native-project-state.json');
 try{const state=JSON.parse(fs.readFileSync(projectStateFile,'utf8'));state.scopes={};delete state.fullSnapshot;delete state.scopeSnapshot;delete state.nativeInputs;fs.writeFileSync(projectStateFile,JSON.stringify(state,null,2)+'\n');}catch(error){if(error.code!=='ENOENT')throw error;}
 const result=await nativeProcess(path.join(working.installPath,'bin/jadloadb.exe'),['path='+working.databasePath,'ini='+working.iniPath,'server=singleUser','schemaFile='+batch,'loadStyle=currentSchemaVersion','replayableReorg=true','dontSaveSources=false','compileUnchangedMethods=true','ShowProgress=true'],{...working,cwd:directory},output);
 const results=staged.map(row=>({...row,success:result.success,code:result.code,diagnostics:compilerDiagnostics(result.output,row.source,row.text,row.staged)}));
 return {...result,results};
}
function decodeNativeText(bytes){return bytes[0]===255&&bytes[1]===254?bytes.subarray(2).toString('utf16le'):bytes.toString('utf8').replace(/^\uFEFF/,'');}
const xml=value=>String(value).replaceAll('&','&amp;').replaceAll('"','&quot;').replaceAll('<','&lt;').replaceAll('>','&gt;');
async function runNativeTests(working,selection,output=()=>{}){
 const directory=path.join(working.root,'runs',crypto.randomUUID());fs.mkdirSync(directory,{recursive:true});const control=path.join(directory,'tests.xml'),logFile=path.join(directory,'results.log');
 const methods=(selection.methods||[]).map(name=>'<method name="'+xml(name)+'"/>').join('');
 fs.writeFileSync(control,'<?xml version="1.0" encoding="utf-8"?>\n<unitTest><reporter logFile="'+xml(logFile)+'"/><options logCallStack="true"/><tests><test name="WorkspaceTests"><schema name="'+xml(selection.schema)+'"/><class name="'+xml(selection.owner)+'">'+methods+'</class></test></tests></unitTest>');
 const result=working.testSession?await working.testSession.run(working,{...selection,controlFile:control,testName:'WorkspaceTests',logFile},output):await nativeProcess(path.join(working.installPath,'bin/jadclient.exe'),['path='+working.databasePath,'ini='+working.iniPath,'server=singleUser','schema=RootSchema','app=JadeUnitTestBatch','startAppParameters',control,'WorkspaceTests',logFile,'endAppParameters'],{...working,cwd:working.root},output);
 const log=fs.existsSync(logFile)?decodeNativeText(fs.readFileSync(logFile)):'';if(log)output(log);const report=nativeTestReport(log,selection);return {...result,code:result.code===0&&report.tests_completed&&report.failures?1:result.code,success:result.success&&report.tests_completed&&report.failures===0,logFile,log,...report};
}
module.exports={prepareWorkingDatabase,loadWorkingFiles,runNativeTests,nativeProcess,isolatedIni,decodeNativeText,workingLocation};
