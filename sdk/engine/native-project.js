'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {parseSource}=require('./source_symbols');
const {schemaFromSource}=require('./config');
const {xmlTree,canonicalNode}=require('./native-source-filter');
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const decode=bytes=>bytes[0]===255&&bytes[1]===254?bytes.subarray(2).toString('utf16le').replace(/^\uFEFF/,''):bytes.toString('utf8').replace(/^\uFEFF/,'');
const indexes=['typeHeaders','interfaceDefs','membershipDefinitions','typeDefinitions','externalKeyDefinitions','memberKeyDefinitions','typeSources'];
const sections=['schemaDefinitions','localeDefinitions','libraryDefinitions','externalFunctions','constantDefinitions','translatableStringDefinitions',...indexes,'inverseDefinitions','databaseDefinitions','exposedListDefinitions','exportedPackageDefinitions','importedPackageDefinitions','externalFunctionSources'];
const inside=(root,file)=>{const r=path.relative(root,file);return r!== '..'&&!r.startsWith('..'+path.sep)&&!path.isAbsolute(r);};
function minimalSchema(name,parent,version){
 if(!/^[A-Za-z_]\w*$/.test(name)||['RootSchema','JadeSchema'].includes(name))throw Error('Invalid user schema name.');
 if(!/^[A-Za-z_]\w*$/.test(parent)||name===parent)throw Error('Invalid parent schema name.');
 if(!/^\d+\.\d+\.\d+$/.test(version))throw Error('Invalid Jade version.');
 return `jadeVersionNumber "${version}";\r\nschemaDefinition\r\n${name} subschemaOf ${parent} completeDefinition;\r\n`;
}
async function scaffoldSchema(root,name,parent,version,bases={app:'RootSchemaApp',global:'RootSchemaGlobal',session:'RootSchemaSession'}){
 const header=minimalSchema(name,parent,version),schemas=path.join(root,'schemas'),folder=path.join(schemas,name),file=path.join(schemas,name+'.scm');
 if(fs.existsSync(folder)||fs.existsSync(file))throw Error('Schema files already exist: '+name);
 for(const value of Object.values(bases))if(!/^[A-Za-z_]\w*$/.test(value))throw Error('Invalid runtime superclass.');
 await fs.promises.mkdir(path.join(folder,'applications'),{recursive:true});await fs.promises.mkdir(path.join(folder,'types'));
 const files=[file];fs.writeFileSync(file,header,{flag:'wx'});
 for(const [owner,base,group] of [[name,bases.app,'applications'],['G'+name,bases.global,'types'],['S'+name,bases.session,'types']]){
  const text=`jadeVersionNumber "${version}";\r\nschemaDefinition\r\n\t${name} subschemaOf ${parent} partialDefinition;\r\ntypeHeaders\r\n\t${owner} subclassOf ${base} transient, sharedTransientAllowed, transientAllowed, subclassSharedTransientAllowed, subclassTransientAllowed;\r\ntypeDefinitions\r\n\t${owner} completeDefinition\r\n\t(\r\n\t)\r\ndatabaseDefinitions\r\ntypeSources\r\n`;
  const target=path.join(folder,group,owner+'.cls');fs.writeFileSync(target,text,{flag:'wx'});files.push(target);
 }
 const map=path.join(folder,'databaseDefinitions.sec'),db=name.length<=24?name.toLowerCase():name.toLowerCase().slice(0,16)+'_'+hash(Buffer.from(name)).slice(0,8);
 fs.writeFileSync(map,`databaseDefinitions\r\n${name}Db\r\n(\r\ndatabaseFileDefinitions\r\n"${db}";\r\ndefaultFileDefinition "${db}";\r\nclassMapDefinitions\r\n${name} in "_usergui";\r\nG${name} in "${db}";\r\nS${name} in "_environ";\r\n)\r\n`,{flag:'wx'});files.push(map);
 return {name,header:file,folder,files};
}
async function listProjectFiles(root){
 root=path.resolve(root);const paths=[];
 async function visit(directory){for(const entry of await fs.promises.readdir(directory,{withFileTypes:true})){
  const file=path.join(directory,entry.name);
  if(entry.isDirectory()&&!['.git','node_modules','dist'].includes(entry.name)&&!fs.existsSync(path.join(file,'.jade-extraction')))await visit(file);
  else if(entry.isFile()&&/\.(cls|scm|sec|ddx|ddb|jcf)$/i.test(file))paths.push(file);
 }}await visit(root);return paths;
}
async function scanProject(root){
 root=path.resolve(root);const paths=await listProjectFiles(root);
 const entries=[];let next=0;
 await Promise.all(Array.from({length:Math.min(8,paths.length)},async()=>{while(next<paths.length){const file=paths[next++],bytes=await fs.promises.readFile(file),text=decode(bytes),extension=path.extname(file).toLowerCase();entries.push({file,extension,text,hash:hash(bytes),bytes});}}));
 const schemas=new Map();
 for(const entry of entries.filter(e=>e.extension==='.scm'||path.basename(e.file).toLowerCase()==='schemadefinitions.sec')){
  const name=schemaFromSource(entry.text);if(!name)throw Error('Schema header has no schema declaration: '+entry.file);
  if(['RootSchema','JadeSchema'].includes(name))continue;
  const parent=/\bsubschemaOf\s+(\w+)/.exec(entry.text)?.[1],folder=entry.extension==='.scm'?path.join(path.dirname(entry.file),name):path.dirname(entry.file);
  const prior=schemas.get(name);if(prior&&prior.header.hash!==entry.hash)throw Error('Conflicting schema headers: '+name);
  schemas.set(name,{name,parent,folder,header:entry,entries:[],classes:[]});
 }
 const headerEntries=new Set([...schemas.values()].map(s=>s.header)),schemaFolders=[...schemas.values()].sort((a,b)=>b.folder.length-a.folder.length),ownersBySchema=new Map([...schemas.keys()].map(name=>[name,new Set()]));
 for(const entry of entries){
  if(headerEntries.has(entry))continue;
  let name;
  if(entry.extension==='.cls')name=schemaFromSource(entry.text);
  else if(entry.extension==='.ddx')name=xmlTree(entry.text,entry.file).attributes.name;
  else name=schemaFolders.find(s=>inside(s.folder,entry.file))?.name;
  const schema=schemas.get(name);if(!schema)continue;
  if(entry.extension==='.sec'&&path.basename(entry.file).toLowerCase()==='schemadefinitions.sec')continue;
  schema.entries.push(entry);
  if(entry.extension==='.cls'){
   const model=parseSource(entry.text,entry.file),owner=model.classes[0]?.name||path.basename(entry.file,'.cls');
   if(model.classes.length>1)throw Error('Native section builds require one class/type per CLS: '+entry.file);
   if(ownersBySchema.get(name).has(owner))throw Error('Multiple class/type files for '+name+'::'+owner);
   ownersBySchema.get(name).add(owner);
   schema.classes.push({...entry,owner,model});
  }
 }
 const ordered=[],visiting=new Set(),visited=new Set();
 function add(schema,packageEdge=false){if(visited.has(schema.name))return;if(visiting.has(schema.name)){if(packageEdge)return;throw Error('Cyclic schema hierarchy: '+schema.name);}visiting.add(schema.name);if(schemas.has(schema.parent))add(schemas.get(schema.parent));
  const imports=schema.entries.filter(e=>/importedPackageDefinitions\.sec$/i.test(e.file)).map(e=>e.text).join('\n');
  for(const candidate of schemas.values())if(candidate!==schema&&new RegExp('\\b'+candidate.name+'\\b').test(imports))add(candidate,true);
  visiting.delete(schema.name);visited.add(schema.name);ordered.push(schema);}
 for(const schema of [...schemas.values()].sort((a,b)=>a.name.localeCompare(b.name)))add(schema);
 return {root,schemas:ordered,entries,migrations:entries.filter(e=>e.extension==='.jcf').sort((a,b)=>a.file.localeCompare(b.file)),hash:hash(Buffer.from(JSON.stringify(entries.map(e=>[path.relative(root,e.file),e.hash]).sort())))};
}
function joinDdx(schema){
 const parts=schema.entries.filter(e=>e.extension==='.ddx');if(!parts.length)return;
 let opening,attributes;const bodies=[],seen=new Set();
 for(const part of parts){const tree=xmlTree(part.text,part.file),match=/<schema\b[^>]*>([\s\S]*)<\/schema>\s*$/.exec(part.text);
  if(!match||tree.attributes.name!==schema.name)throw Error('Invalid DDX schema identity: '+part.file);
  const identity=JSON.stringify(Object.entries(tree.attributes).filter(([key])=>key!=='CompleteDefinition').sort());
  if(attributes&&attributes!==identity)throw Error('Incompatible DDX schema headers: '+schema.name);attributes=identity;
  for(const child of tree.children){const key=child.tag+'::'+(child.attributes.name||canonicalNode(child));if(seen.has(key))throw Error('Duplicate DDX definition '+key+' in '+schema.name);seen.add(key);}
  opening||=match[0].slice(0,match[0].indexOf('>')+1).replace(/CompleteDefinition="[^"]*"/,'CompleteDefinition="true"');bodies.push(match[1]);
 }
 return '<?xml version="1.0" encoding="UTF-8"?>\r\n'+opening+bodies.join('')+'</schema>\r\n';
}
async function stageProject(project,destination,{signal}={}){
 const cancelled=()=>{if(signal?.aborted)throw Error('Native source staging cancelled.');};cancelled();
 await fs.promises.mkdir(destination,{recursive:true});const schemas=[];
 for(const [schemaIndex,schema] of project.schemas.entries()){
  const shortName='s'+schemaIndex,directory=path.join(destination,shortName),types=path.join(directory,'Types');await fs.promises.mkdir(types,{recursive:true});
  for(const section of sections)fs.writeFileSync(path.join(directory,section+'.sec'),'');
  for(const entry of schema.entries.filter(e=>e.extension==='.sec'))fs.writeFileSync(path.join(directory,path.basename(entry.file)),'\uFEFF'+entry.text);
  fs.writeFileSync(path.join(directory,'schemaDefinitions.sec'),'\uFEFF'+schema.header.text);
  const indexed=new Map(indexes.map(name=>[name,[]]));
  let next=0;
  await Promise.all(Array.from({length:Math.min(8,schema.classes.length)},async()=>{while(next<schema.classes.length){
   cancelled();const entry=schema.classes[next++];
   let text=entry.text;
   // JadeJoinSchema requires this native section boundary, even for classes
   // without map definitions. Only the private staging copy is normalized.
   if(!/^databaseDefinitions\s*$/m.test(entry.model.code))text=text.replace(/^typeSources\s*$/m,'databaseDefinitions\r\n\r\ntypeSources');
   if(!/^databaseDefinitions\s*$/m.test(parseSource(text,entry.file).code))text+='\r\ndatabaseDefinitions\r\n\r\ntypeSources\r\n';
   // The native joiner recognizes section boundaries by indentation. A CLS
   // accepted by the loader can still lack the indentation produced by extract.
   // Indent only unindented metadata, preserving literals and method bodies.
   const lexical=parseSource(text,entry.file).code.split('\n');let inSources=false;
   text=text.split('\n').map((line,index)=>{const code=lexical[index]||'';
    if(/^typeSources\s*$/.test(code)){inSources=true;return line;}
    if(inSources||!code.trim()||/^\s/.test(code)||/^(?:jadeVersionNumber\b|schemaDefinition\s*$|typeHeaders\s*$|interfaceDefs\s*$|membershipDefinitions\s*$|typeDefinitions\s*$|externalKeyDefinitions\s*$|memberKeyDefinitions\s*$|databaseDefinitions\s*$)/.test(code))return line;
    return '\t'+line;
   }).join('\n');
   await fs.promises.writeFile(path.join(types,entry.owner+'.cls'),'\uFEFF'+text.replace(/\r\n|\r|\n/g,'\r\n'));
   for(const name of indexes)if(new RegExp('^'+name+'\\s*$','m').test(entry.model.code))indexed.get(name).push(entry.owner);
  }}));cancelled();
  for(const [name,owners] of indexed)fs.writeFileSync(path.join(directory,name+'.sec'),'\uFEFF'+(owners.length?owners.sort().join('\r\n')+'\r\n':''));
  const ddx=joinDdx(schema),ddxFile=ddx?path.join(destination,shortName+'.ddx'):undefined;if(ddx)fs.writeFileSync(ddxFile,ddx);
  schemas.push({...schema,directory,output:path.join(destination,shortName+'.scm'),ddxFile});
 }
 return {...project,directory:destination,schemas};
}
module.exports={scanProject,listProjectFiles,stageProject,minimalSchema,scaffoldSchema,joinDdx,decode,hash,indexes};
