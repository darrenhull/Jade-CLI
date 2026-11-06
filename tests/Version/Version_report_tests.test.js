'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),os=require('node:os'),path=require('node:path'),{spawnSync}=require('node:child_process');
test('deployed help, doctor and broker status report the SDK version without a root package manifest',async t=>{
 const root=fs.mkdtempSync(path.join(os.tmpdir(),'jade-version-test-'));t.after(()=>fs.rmSync(root,{recursive:true,force:true}));
 const repository=path.resolve(__dirname,'../..');
 for(const folder of ['bin','sdk'])fs.cpSync(path.join(repository,folder),path.join(root,folder),{recursive:true,filter:file=>!file.endsWith('.exe')});
 const manifestFile=path.join(root,'sdk/package.json'),manifest=JSON.parse(fs.readFileSync(manifestFile));manifest.version='1.2.3';fs.writeFileSync(manifestFile,JSON.stringify(manifest));
 const result=spawnSync(process.execPath,[path.join(root,'bin/jade-cli.js'),'help'],{encoding:'utf8'});assert.equal(result.status,0,result.stderr);assert.match(result.stdout,/jade-cli 1\.2\.3\b/);
 const {JadeEngine}=require(path.join(root,'sdk'));const engine=new JadeEngine();try{const doctor=await engine.execute({command:'doctor',config:{}});assert.equal(doctor.sdkVersion,'1.2.3');}finally{await engine.close();}
 const env={...process.env,JADE_TOOLS_STATE_DIRECTORY:path.join(root,'state')};
 const run=command=>spawnSync(process.execPath,[path.join(root,'bin/jade-cli.js'),command,'--json'],{encoding:'utf8',env,timeout:15000});
 try{const status=run('status');assert.equal(status.status,0,status.stderr);assert.equal(JSON.parse(status.stdout).sdkVersion,'1.2.3');const owner=JSON.parse(fs.readFileSync(path.join(root,'state/owner.json'),'utf8'));assert.equal(owner.sdkVersion,'1.2.3');}
 finally{run('shutdown');}
});
