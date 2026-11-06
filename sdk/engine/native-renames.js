'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),crypto=require('node:crypto');
const {parseSource}=require('./source_symbols');
const {startExtract,resolveExtractConfig}=require('./extract');
const {nativeProcess}=require('./working-database');
function readRenames(text,file){
 const version=/^\s*JadeVersionNumber\s+"?([\d.]+)"?\s*$/im.exec(text)?.[1];if(!/^\s*JadeCommandFile\b/i.test(text)||!version)throw Error('Invalid native command file: '+file);
 const operations=[];
 for(const line of text.split(/\r?\n/).map(line=>line.trim()).filter(Boolean)){
  if(/^(?:JadeCommandFile|JadeVersionNumber\b|Commands$|\/\/|AbortOnError\s+(?:True|False)$)/i.test(line))continue;
  const match=/^Rename\s+Class\s+([A-Za-z_]\w*)::([A-Za-z_]\w*)\s+([A-Za-z_]\w*)\s*$/i.exec(line);
  if(!match)throw Error('Tracked native migrations accept Rename Class commands: '+file+' ('+line+').');
  operations.push({file,version,schema:match[1],old:match[1]+'::'+match[2],oldName:match[2],newName:match[3],next:match[1]+'::'+match[3]});
 }return operations;
}
function sourceClasses(project){const names=new Set();for(const schema of project.schemas)for(const cls of schema.classes)names.add(schema.name+'::'+cls.owner);return names;}
function planRenames(operations,current,source){
 const names=new Set(current),pending=[];
 for(const op of operations){
  if(!source.has(op.next)){const successors=new Map(operations.map(o=>[o.old,o.next]));let final=op.next;const seen=new Set();while(successors.has(final)&&!seen.has(final)){seen.add(final);final=successors.get(final);}if(!source.has(final))throw Error('Rename the source declarations/references to '+op.next+' before applying '+op.file);}
  if(names.has(op.old)&&names.has(op.next))throw Error('Rename source and target both exist: '+op.old+' / '+op.next);
  if(names.has(op.old)){pending.push(op);names.delete(op.old);names.add(op.next);}
 }return pending;
}
async function currentClasses(working,output){
 await working.beforeNativeProcess?.();
 const temporary=fs.mkdtempSync(path.join(os.tmpdir(),'j'));
 try{
  const result=await startExtract(resolveExtractConfig(working),path.join(temporary,'r'),output).completion;if(!result.success)throw Error('Could not inspect classes before native rename.');
  const manifest=JSON.parse(fs.readFileSync(path.join(result.destination,'extract-manifest.json'),'utf8')),names=new Set();
  for(const file of manifest.files.filter(e=>/\.cls$/i.test(e.path))){const model=parseSource(fs.readFileSync(path.join(result.destination,file.path),'utf8'));for(const cls of model.classes)names.add(model.schema+'::'+cls.name);}
  return names;
 }finally{if(path.dirname(path.resolve(temporary))===path.resolve(os.tmpdir())&&path.basename(temporary).startsWith('j'))fs.rmSync(temporary,{recursive:true,force:true});}
}
function projectRenames(project){return (project.migrations||[]).flatMap(e=>readRenames(e.text,e.file));}
async function applyRenames(working,project,output=()=>{},knownClasses){
 const operations=projectRenames(project);if(!operations.length)return [];
 const signature=crypto.createHash('sha256').update(JSON.stringify(project.migrations.map(e=>[e.file,e.hash]))).digest('hex'),ledger=path.join(working.root,'renames-applied.json');
 if(!knownClasses&&fs.existsSync(ledger)&&JSON.parse(fs.readFileSync(ledger,'utf8')).signature===signature)return [];
 const pending=planRenames(operations,knownClasses||await currentClasses(working,output),sourceClasses(project));
 if(pending.length){
  const directory=fs.mkdtempSync(path.join(os.tmpdir(),'jm-'));
  try{
   for(const file of [...new Set(pending.map(op=>op.file))]){
    const selected=pending.filter(op=>op.file===file),command=path.join(directory,'rename.jcf');
    fs.writeFileSync(command,'JadeCommandFile\r\nJadeVersionNumber '+selected[0].version+'\r\nCommands\r\n'+selected.map(op=>'Rename Class '+op.old+' '+op.newName).join('\r\n')+'\r\n');
    output(selected.map(op=>'Native RENAME '+op.old+' -> '+op.next).join('\n')+'\n');
    const result=await nativeProcess(path.join(working.installPath,'bin/jadloadb.exe'),['path='+working.databasePath,'ini='+working.iniPath,'server='+(working.server||'singleUser'),'commandFile='+command,'loadStyle=currentSchemaVersion','replayableReorg=true','showProgress=true'],{...working,cwd:directory},output);
    if(!result.success)throw Error('Native class rename failed (exit '+result.code+'). '+result.output);
   }
  }finally{if(path.dirname(path.resolve(directory))===path.resolve(os.tmpdir())&&path.basename(directory).startsWith('jm-'))fs.rmSync(directory,{recursive:true,force:true});}
 }
 if(!knownClasses)fs.writeFileSync(ledger,JSON.stringify({signature},null,2)+'\n');return pending;
}
module.exports={readRenames,planRenames,applyRenames,projectRenames,sourceClasses};
