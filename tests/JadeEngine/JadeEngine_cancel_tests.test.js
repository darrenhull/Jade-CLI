'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const {JadeEngine}=require('../../sdk');
test('cancellation reaches active native operation and does not rewrite result',async()=>{
 let finish,stopped=false;const e=new JadeEngine({NativeTestSession:class {async release(){}},startWorking(){return {completion:new Promise(r=>finish=r),stop(){stopped=true;finish({code:2,passed:false,cancelled:true});}};}});
 const request=e.execute({command:'build',config:{}},()=>{},'job1');await new Promise(setImmediate);assert.equal(e.cancel('job1'),true);assert.equal((await request).cancelled,true);assert.equal(stopped,true);await e.close();
});
