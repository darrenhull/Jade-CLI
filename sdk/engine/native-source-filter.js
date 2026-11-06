'use strict';
const fs=require('node:fs'),path=require('node:path');
const sax=require('../vendor/sax');

function isEmptyInheritedType(text){
  const headers=/\btypeHeaders\b([\s\S]*?)\btypeDefinitions\b/.exec(text);
  if(!headers||headers[1].trim())return false;
  const definition=/\btypeDefinitions\b([\s\S]*?)(?=\bdatabaseDefinitions\b|\btypeSources\b|$)/.exec(text)?.[1];
  if(!definition)return false;
  const body=definition.replace(/setModifiedTimeStamp\b[^;]*;/g,'').replace(/\b\w+\s+completeDefinition\b/g,'').replace(/[()\s]/g,'');
  const source=text.split(/\btypeSources\b/)[1]||'';
  return !body&&!source.trim();
}
function typeDeclaration(text){
  const header=/\btypeHeaders\b([\s\S]*?)\btypeDefinitions\b/.exec(text)?.[1]||'';
  const name=/\b(\w+)\s+subclassOf\b/.exec(header)?.[1],number=/\bnumber\s*=\s*(\d+)/.exec(header)?.[1];
  if(!name||!number)return;
  const body=text.slice(text.indexOf('typeHeaders')).split('databaseDefinitions')[0];
  const sources=text.split(/\btypeSources\b/)[1]||'';
  const normalized=(body+'\ntypeSources\n'+sources).replace(/setModifiedTimeStamp\b[^;]*;/g,'').replace(/\r\n/g,'\n').trim();
  return {name,number,normalized};
}

function xmlTree(text,label){
  const parser=sax.parser(true,{xmlns:false,trim:false,normalize:false}),stack=[];let root,error;
  parser.ondoctype=()=>{throw Error('DDX XML DOCTYPE is not supported: '+label);};
  parser.onopentag=node=>{const value={tag:node.name,attributes:node.attributes,children:[],text:''};if(stack.length)stack.at(-1).children.push(value);else if(root)throw Error('Multiple DDX XML roots: '+label);else root=value;stack.push(value);};
  parser.ontext=text=>{if(stack.length)stack.at(-1).text+=text;else if(text.trim())throw Error('Text outside DDX XML: '+label);};
  parser.oncdata=text=>{if(stack.length)stack.at(-1).text+=text;};parser.onclosetag=()=>stack.pop();parser.onerror=value=>{error=value;};
  try{parser.write(text.replace(/^\uFEFF/,'')).close();}catch(value){error=value;}
  if(error||!root||root.tag!=='schema')throw Error('Invalid DDX XML '+label+': '+(error?.message||'expected schema root'));
  return root;
}
function canonicalNode(node){return JSON.stringify([node.tag,Object.entries(node.attributes).sort(([a],[b])=>a.localeCompare(b)),node.children.length?node.text.trim():node.text,node.children.map(canonicalNode)]);}
function verifyDdxCoverage(raw,schema,files,omitted){
  if(files.some(file=>file.path===schema+'.ddb'))throw Error('Section-only source requires ddxFormat; cannot safely omit binary DDB for '+schema);
  const aggregate=files.find(file=>file.path===schema+'.ddx');if(!aggregate)return;
  const full=xmlTree(fs.readFileSync(path.join(raw,aggregate.path),'utf8'),aggregate.path);
  if(full.text.trim())throw Error('DDX coverage cannot omit inline schema data: '+aggregate.path);
  if(full.attributes.name!==schema)throw Error('DDX schema identity mismatch: '+aggregate.path);
  const pieces=new Map();
  for(const file of files.filter(file=>file.path.startsWith(schema+'/Types/')&&file.path.endsWith('.ddx')&&!omitted.has(file.path))){
    const part=xmlTree(fs.readFileSync(path.join(raw,file.path),'utf8'),file.path);
    if(part.attributes.name!==schema)throw Error('DDX schema identity mismatch: '+file.path);
    const attributes=node=>Object.fromEntries(Object.entries(node.attributes).filter(([key])=>key!=='CompleteDefinition').sort(([a],[b])=>a.localeCompare(b)));
    if(JSON.stringify(attributes(full))!==JSON.stringify(attributes(part)))throw Error('DDX schema attributes differ: '+file.path);
    for(const node of part.children){const key=canonicalNode(node);pieces.set(key,(pieces.get(key)||0)+1);}
  }
  for(const node of full.children){const key=canonicalNode(node);if(!pieces.get(key))throw Error('Individual DDX coverage is incomplete for '+schema+' '+node.tag+' '+(node.attributes.name||''));pieces.set(key,pieces.get(key)-1);}
  if([...pieces.values()].some(count=>count!==0))throw Error('Individual DDX coverage has extra or duplicated objects for '+schema);
}
module.exports={isEmptyInheritedType,typeDeclaration,verifyDdxCoverage,xmlTree,canonicalNode};
