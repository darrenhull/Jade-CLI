'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const {JadeEngine}=require('../sdk');
test('build/test reuse one native session and return unchanged backend reports',async()=>{
 let sessions=0,releases=0;const seen=[];
 const e=new JadeEngine({NativeTestSession:class {constructor(){sessions++;}async release(){releases++;}},startWorking(config,op,output){seen.push({config,op});output('native output');return {completion:Promise.resolve({code:0,passed:true,report:{results:[]}}),stop(){}};}});
 const config={databasePath:'target',installPath:'runtime',iniPath:'target/ini',workspaceRoots:['source'],storagePath:'state'};
 const lines=[];assert.equal((await e.execute({command:'build',config},x=>lines.push(x))).passed,true);
 await e.execute({command:'test',config});assert.equal(sessions,1);assert.equal(seen[0].op,'compile');assert.equal(seen[1].op,'test.run');assert.equal(seen[0].config.testSession,seen[1].config.testSession);assert.deepEqual(lines,['native output']);
 await e.close();assert.ok(releases>=1);
});
test('cancellation reaches active native operation and does not rewrite result',async()=>{
 let finish,stopped=false;const e=new JadeEngine({NativeTestSession:class {async release(){}},startWorking(){return {completion:new Promise(r=>finish=r),stop(){stopped=true;finish({code:2,passed:false,cancelled:true});}};}});
 const request=e.execute({command:'build',config:{}},()=>{},'job1');await new Promise(setImmediate);assert.equal(e.cancel('job1'),true);assert.equal((await request).cancelled,true);assert.equal(stopped,true);await e.close();
});
test('native.run rejects executables outside installed Jade allowlist',async()=>{
 const e=new JadeEngine();await assert.rejects(e.execute({command:'native.run',config:{installPath:'C:/Jade'},executable:'C:/Windows/System32/cmd.exe',args:[]}),/allowlist/i);await e.close();
});
test('inspection sessions reject target substitution before native access',async()=>{
 let opened=0;const e=new JadeEngine({inspectionTarget:(c,t)=>({...c,selection:t}),InspectorTransport:class {constructor(){opened++;this.ready=Promise.resolve();this.exited=Promise.resolve();}on(){}async request(){return {schemas:[]};}async stop(){}}});
 const config={installPath:'runtime',databasePath:'source',iniPath:'source.ini'};
 await e.execute({command:'inspect.catalog',target:'source',config});
 await assert.rejects(e.execute({command:'inspect.catalog',target:'source',config:{...config,databasePath:'another'}}),/target changed/i);
 assert.equal(opened,1);await e.close();
});
test('build wire result excludes internal project bundles and source bytes',async()=>{
 const huge={entries:[{bytes:Buffer.alloc(10*1024*1024),text:'source'.repeat(100000)}]};
 const e=new JadeEngine({NativeTestSession:class{async release(){}},startWorking(){return{completion:Promise.resolve({code:0,passed:true,report:{compiled:true,loaded_files:30000},bundle:huge,project:huge}),stop(){}};}});
 const result=await e.execute({command:'build',config:{}});assert.equal(result.bundle,undefined);assert.equal(result.project,undefined);assert.ok(JSON.stringify(result).length<200);assert.equal(result.report.loaded_files,30000);await e.close();
});
