'use strict';
const fs=require('node:fs');
const {parseSource}=require('./source_symbols');
function testsInSource(text,source){const model=parseSource(text,source);return model.classes.flatMap(cls=>cls.members.filter(m=>m.bodyStart!==undefined&&/\bunitTest\b/i.test(m.flags||'')&&!/\bunitTestIgnore\b/i.test(m.flags||'')).map(m=>({schema:model.schema,owner:cls.name,method:m.name,source,key:model.schema+'::'+cls.name+'::'+m.name})));}
function testCatalog(files){return files.flatMap(file=>testsInSource(fs.readFileSync(file,'utf8'),file));}
function compareTests(expected,actual){const remote=new Map(expected.map(e=>[e.key,e])),local=new Map(actual.map(e=>[e.key,e]));const missing=[...remote.values()].filter(e=>!local.has(e.key));return {expected:remote.size,discovered:local.size,missing,localOnly:[...local.values()].filter(e=>!remote.has(e.key)),verified:remote.size>0&&!missing.length};}
module.exports={testsInSource,testCatalog,compareTests};
