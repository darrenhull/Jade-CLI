'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),crypto=require('node:crypto');
const {startWorking}=require('./engine/headless-runner');
const {NativeTestSession}=require('./engine/test-session');
const {nativeProcess}=require('./engine/working-database');
const {InspectorTransport}=require('./engine/object-transport');
const {inspectionTarget}=require('./engine/object-session');
const {parseSource}=require('./engine/source_symbols');
const {testCatalog}=require('./engine/test-catalog');
const {scanProject}=require('./engine/native-project');
async function sourceFiles(roots){const projects=await Promise.all(roots.map(root=>scanProject(root)));return [...new Set(projects.flatMap(p=>p.entries.filter(e=>e.extension==='.cls').map(e=>e.file)))].sort();}
class JadeEngine{
 constructor(dependencies={}){this.dependencies={startWorking,NativeTestSession,nativeProcess,InspectorTransport,inspectionTarget,...dependencies};this.active=new Map();this.inspections=new Map();this.inspectionTargets=new Map();}
 cancel(id){const operation=this.active.get(id);if(!operation)return false;operation.stop();return true;}
 async release(){if(this.session)await this.session.release();await this.releaseInspections();}
 async close(){for(const id of this.active.keys())this.cancel(id);await this.release();}
 async execute(request,output=()=>{},id=crypto.randomUUID()){
  const command=request.command,config={...request.config};
  if(command==='disconnect'){await this.release();return {code:0,passed:true};}
  if(command==='doctor')return {code:0,passed:true,platform:process.platform,sdkVersion:'0.1.0',runtime:config.installPath||null,tools:Object.fromEntries(['jadclient','jadloadb','jdbutilb','jdbadmin'].map(n=>[n,!!config.installPath&&fs.existsSync(path.join(config.installPath,'bin',n+'.exe'))])),capabilities:{build:true,test:true,sourceSearch:true,inspection:true,nativeUtility:true,debug:false,publishPlan:false}};
  if(command==='source.search'||command==='tests.list'){
   const files=await sourceFiles(config.workspaceRoots||[config.workspace||process.cwd()]);let rows;
   if(command==='tests.list')rows=testCatalog(files);else rows=files.flatMap(file=>{const model=parseSource(fs.readFileSync(file,'utf8'),file);return model.classes.map(c=>({schema:model.schema,name:c.name,source:file}));});
   const query=String(request.query||'').toLowerCase();rows=rows.filter(row=>!query||JSON.stringify([row.schema,row.name,row.owner,row.method]).toLowerCase().includes(query));const offset=Math.max(0,Number(request.offset)||0),limit=Math.min(100,Math.max(1,Number(request.limit)||100));return {code:0,passed:true,total:rows.length,offset,rows:rows.slice(offset,offset+limit)};
  }
  if(command==='build'||command==='test'){
   await this.releaseInspections();
   this.session||=new this.dependencies.NativeTestSession({executable:path.join(__dirname,'native/jade-test-helper.exe')});
   config.testSession=this.session;config.beforeNativeProcess=()=>this.session.release();
   const operation=this.dependencies.startWorking(config,command==='build'?'compile':'test.run',output);this.active.set(id,operation);
   try{const result=await operation.completion;return Object.fromEntries(['code','passed','report','cancelled','timedOut','signal'].filter(k=>result[k]!==undefined).map(k=>[k,result[k]]));}finally{this.active.delete(id);}
  }
  if(command==='native.run'){
   const executable=path.resolve(request.executable||''),install=path.resolve(config.installPath||''),name=path.basename(executable).toLowerCase();
   if(!config.installPath||!['jadclient.exe','jadloadb.exe','jdbutilb.exe','jdbadmin.exe'].includes(name)||executable.toLowerCase()!==path.resolve(install,'bin',name).toLowerCase())throw Error('Native executable is outside the Jade utility allowlist.');
   if(!Array.isArray(request.args)||request.args.some(a=>typeof a!=='string'||a.includes('\0')))throw Error('Native utility args must be strings.');
   await this.release();const abort=new AbortController();this.active.set(id,{stop:()=>abort.abort()});
   try{return await this.dependencies.nativeProcess(executable,request.args,{...request.options,signal:abort.signal,beforeNativeProcess:undefined,testSession:undefined},output);}finally{this.active.delete(id);}
  }
  if(command.startsWith('inspect.'))return this.inspect(request,config);
  throw Error('Unsupported SDK operation: '+command);
 }
 async releaseInspections(){for(const client of this.inspections.values()){await client.stop();await client.exited;}this.inspections.clear();this.inspectionTargets.clear();}
 async releaseClientInspections(prefix){for(const [id,client] of this.inspections)if(id.startsWith(prefix)){await client.stop();await client.exited;this.inspections.delete(id);this.inspectionTargets.delete(id);}}
 async inspect(request,config){
  const command=request.command.slice(8),sessionId=request.sessionId||'default';
  if(command==='close'){const client=this.inspections.get(sessionId);if(client){await client.stop();await client.exited;this.inspections.delete(sessionId);this.inspectionTargets.delete(sessionId);}return {code:0,passed:true};}
  if(!['catalog','instances','object','collection','release'].includes(command))throw Error('Unsupported inspection command.');
  const target=this.dependencies.inspectionTarget(config,request.target||'working');
  const identity=JSON.stringify([request.target||'working',target.installPath,target.databasePath,target.iniPath,target.server,target.schema,target.user].map(x=>typeof x==='string'?x.toLowerCase():x));
  if(this.inspections.has(sessionId)&&this.inspectionTargets.get(sessionId)!==identity)throw Error('Inspection target changed. Close the existing session before connecting to another database.');
  if(this.session)await this.session.release();let client=this.inspections.get(sessionId);
  if(!client){const folder=path.join(os.tmpdir(),'jade-tools-inspection');fs.mkdirSync(folder,{recursive:true});const file=path.join(folder,crypto.randomUUID()+'.json');fs.writeFileSync(file,JSON.stringify({...target,readOnly:true}),{mode:0o600});client=new this.dependencies.InspectorTransport(path.join(__dirname,'native/jade-inspect-helper.exe'),file);client.on('failure',()=>{});try{await client.ready;this.inspections.set(sessionId,client);this.inspectionTargets.set(sessionId,identity);}catch(error){await client.stop();throw error;}finally{fs.rmSync(file,{force:true});}}
  const args={...request.args};if(args.limit!==undefined)args.limit=Math.min(100,Math.max(1,Number(args.limit)||100));return {code:0,passed:true,sessionId,body:await client.request(command,args)};
 }
}
module.exports={JadeEngine,sourceFiles};
