'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),os=require('node:os'),path=require('node:path'),{spawnSync}=require('node:child_process');
const executable=process.env.JADE_CLI_STANDALONE_EXE;
test('standalone executable runs help and source query without Node on PATH',{skip:!executable},()=>{
 const root=fs.mkdtempSync(path.join(os.tmpdir(),'jade-sea-proof-'));
 const env={...process.env,PATH:path.join(process.env.SystemRoot||'C:/Windows','System32'),NODE_OPTIONS:'',JADE_TOOLS_STATE_DIRECTORY:path.join(root,'state')};
 const run=args=>spawnSync(executable,args,{cwd:root,env,encoding:'utf8',windowsHide:true,timeout:20000});
 try{
  const help=run(['help']);assert.ifError(help.error);assert.equal(help.status,0,help.stderr);assert.match(help.stdout,/jade-cli/);
  fs.writeFileSync(path.join(root,'Fish.cls'),'jadeVersionNumber "25.0.02";\r\nschemaDefinition\r\nAquarium subschemaOf RootSchema partialDefinition;\r\ntypeHeaders\r\nFish subclassOf Object transient;\r\ntypeDefinitions\r\nFish completeDefinition\r\n(\r\n)\r\ntypeSources\r\n');
  const found=run(['source','search','--workspace',root,'--query','Fish','--json']);assert.ifError(found.error);assert.equal(found.status,0,found.stderr);const result=JSON.parse(found.stdout);assert.equal(result.total,1);assert.equal(result.rows[0].schema,'Aquarium');
  const status=run(['status','--json']);assert.equal(status.status,0,status.stderr);assert.ok(JSON.parse(status.stdout).brokerPID>0);
 }finally{run(['shutdown','--json']);fs.rmSync(root,{recursive:true,force:true});}
});
