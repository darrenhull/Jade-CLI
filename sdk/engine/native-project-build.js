'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const os=require('node:os');
const {stageProject,decode}=require('./native-project');
const {nativeProcess}=require('./working-database');
const {compilerDiagnostics}=require('./native-reports');
const {parseSource,sourcePosition}=require('./source_symbols');
async function joinProject(working,project,output=()=>{}){
 // Some supported Jade utilities still enforce MAX_PATH. Keep private native
 // inputs short even when VS Code global storage or the source root is deep.
 const identity=crypto.createHash('sha256').update(path.resolve(working.root).toLowerCase()).digest('hex').slice(0,8);
 const staged=await stageProject(project,path.join(os.tmpdir(),'jd-'+identity,crypto.randomUUID().slice(0,8)),{signal:working.signal});
 for(const schema of staged.schemas){
  output('Rejoining native schema '+schema.name+' ...\n');
  const log=path.join(working.root,'logs/jadejoinschema.log'),previous=fs.existsSync(log)?decode(fs.readFileSync(log)):'';
  const result=await nativeProcess(path.join(working.installPath,'bin/jadclient.exe'),['path='+working.databasePath,'ini='+working.iniPath,'server=singleUser','schema=JadeSchema','app=JadeJoinSchema','startAppParameters',schema.directory,schema.output,'endAppParameters'],{...working,cwd:staged.directory},output);
  const errors=fs.existsSync(log)?decode(fs.readFileSync(log)).slice(previous.length):'';
  if(!result.success||/Unhandled\s+(?:System)?Exception/i.test(errors))throw Error('Native schema rejoin failed for '+schema.name+'. '+(errors||result.output));
  if(!fs.existsSync(schema.output))throw Error('Native join did not create '+schema.name+'.scm');
  const text=decode(fs.readFileSync(schema.output));
  if(schema.classes.length&&!/^typeDefinitions\s*$/m.test(text))throw Error('Native join produced a truncated schema: '+schema.name);
  const model=parseSource(text,schema.output+'.cls');
  const joinedOwners=new Set(model.classes.map(value=>value.name));
  for(const cls of schema.classes)if(!joinedOwners.has(cls.owner))throw Error('Native join omitted '+schema.name+'::'+cls.owner);
  schema.text=text;
 }
 const batch=path.join(staged.directory,'workspace.mul');
 fs.writeFileSync(batch,'#MULTIPLE_SCHEMA_EXTRACT\r\n'+staged.schemas.map(s=>path.basename(s.output)+(s.ddxFile?' '+path.basename(s.ddxFile):'')).join('\r\n')+'\r\n');
 return {...staged,batch};
}
function projectDiagnostics(output,bundle){
 const diagnostics=[];
 for(const schema of bundle.schemas){
  for(const cls of schema.classes){const owners=new Set(cls.model.classes.map(c=>c.name));diagnostics.push(...compilerDiagnostics(output,cls.file,cls.text,schema.output).filter(d=>d.owner&&owners.has(d.owner)));}
  // Structural loader locations use the assembled SCM's line numbers. Match
  // the offending native line back to the unchanged workspace source.
  for(const error of compilerDiagnostics(output,schema.output,schema.text,schema.output).filter(d=>!d.owner)){
   const line=schema.text.split(/\r?\n/)[error.line-1]?.trim(),matches=[];
   if(line)for(const entry of [schema.header,...schema.entries]){const lines=entry.text.split(/\r?\n/);for(let i=0;i<lines.length;i++)if(lines[i].trim()===line)matches.push({source:entry.file,line:i+1});}
   diagnostics.push(matches.length===1?{...error,...matches[0]}:error);
  }
 }
 return diagnostics;
}
async function loadProject(working,bundle,output=()=>{},options={}){
 output('Loading native schema resources into '+working.databasePath+' ...\n');
 const result=await nativeProcess(path.join(working.installPath,'bin/jadloadb.exe'),['path='+working.databasePath,'ini='+working.iniPath,'server='+(working.server||'singleUser'),'schemaFile='+bundle.batch,'loadStyle=currentSchemaVersion','replayableReorg=true','dontSaveSources=false','compileUnchangedMethods=true','allowCircularPackages=true','deleteIfAbsent='+(options.deleteIfAbsent!==false),'ShowProgress=true'],{...working,cwd:bundle.directory},output);
 return {...result,diagnostics:projectDiagnostics(result.output,bundle)};
}
module.exports={joinProject,loadProject,projectDiagnostics};
