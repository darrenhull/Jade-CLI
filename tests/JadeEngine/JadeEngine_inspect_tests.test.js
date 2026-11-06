'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs');
const {JadeEngine}=require('../../sdk');

function harness(t,{constructorError,readyError}={}) {
  let file,resolveExit,signalStopped;
  const exited=new Promise(resolve=>{resolveExit=resolve;});
  const stopped=new Promise(resolve=>{signalStopped=resolve;});
  class Transport {
    constructor(executable,session) {
      file=session;
      if(constructorError)throw constructorError;
      this.ready=readyError?Promise.reject(readyError):Promise.resolve();
      this.exited=exited;
    }
    on() {}
    async request() {return {schemas:[]};}
    async stop() {signalStopped();}
  }
  const engine=new JadeEngine({inspectionTarget:config=>config,InspectorTransport:Transport});
  t.after(()=>{resolveExit();if(file)fs.rmSync(file,{force:true});});
  return {engine,stopped,exit:resolveExit,get file(){return file;}};
}
const request={command:'inspect.catalog',target:'source',config:{databasePath:'fixture',iniPath:'fixture.ini'}};

test('inspection retains its owned configuration until the native process exits',async t=>{
  const h=harness(t);
  assert.equal((await h.engine.execute(request)).passed,true);
  assert.equal(fs.existsSync(h.file),true,'Windows native readers can hold the file until process exit.');
  const closing=h.engine.close();
  await h.stopped;
  assert.equal(fs.existsSync(h.file),true,'A close request is not confirmation that the process exited.');
  h.exit();await closing;
  assert.equal(fs.existsSync(h.file),false);
});
test('inspection removes an owned configuration if transport construction fails',async t=>{
  const h=harness(t,{constructorError:Error('could not spawn inspector')});
  await assert.rejects(h.engine.execute(request),/could not spawn inspector/);
  assert.equal(fs.existsSync(h.file),false);
  await h.engine.close();
});
test('failed signon waits for actual transport exit before removing its configuration',async t=>{
  const h=harness(t,{readyError:Error('signon failed')});
  const rejected=assert.rejects(h.engine.execute(request),/signon failed/);
  await h.stopped;await new Promise(setImmediate);
  try{assert.equal(fs.existsSync(h.file),true,'Failure cleanup must wait for the native file handle to close.');}
  finally{h.exit();await rejected;}
  assert.equal(fs.existsSync(h.file),false);
  await h.engine.close();
});
