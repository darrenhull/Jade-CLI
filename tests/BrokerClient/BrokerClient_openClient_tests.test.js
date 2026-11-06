'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),os=require('node:os'),path=require('node:path'),{spawn}=require('node:child_process');
test('independent stdio clients share authenticated broker; source queries need no Jade runtime',async()=>{
 const root=fs.mkdtempSync(path.join(os.tmpdir(),'jade-cli-service-')),env={...process.env,JADE_TOOLS_STATE_DIRECTORY:path.join(root,'state')};
 const entry=path.resolve(__dirname,'../../bin/jade-cli.js');const clients=[];
 function open(){const child=spawn(process.execPath,[entry,'service','--stdio'],{env,windowsHide:true});clients.push(child);let buffer='',seq=0;const pending=new Map();child.stdout.on('data',b=>{buffer+=b;let n;while((n=buffer.indexOf('\n'))>=0){const p=JSON.parse(buffer.slice(0,n));buffer=buffer.slice(n+1);if(p.event==='result'){pending.get(p.id)?.(p.result);pending.delete(p.id);}}});return request=>new Promise(resolve=>{const id=String(++seq);pending.set(id,resolve);child.stdin.write(JSON.stringify({id,...request})+'\n');});}
 const a=open(),b=open();try{const first=await a({command:'status'}),second=await b({command:'status'});assert.equal(first.brokerPID,second.brokerPID);assert.ok(first.brokerPID>0);const doctor=await a({command:'doctor'});assert.equal(doctor.capabilities.build,true);
 fs.mkdirSync(path.join(root,'workspace'));fs.writeFileSync(path.join(root,'workspace','Fish.cls'),'jadeVersionNumber "25.0.02";\nschemaDefinition\nSJetSchema subschemaOf RootSchema partialDefinition;\ntypeHeaders\nFish subclassOf Object transient;\ntypeDefinitions\nFish completeDefinition\n(\n)\ntypeSources\n');
 const found=await b({command:'source.search',config:{workspaceRoots:[path.join(root,'workspace')]},query:'Fish'});assert.equal(found.total,1);assert.equal(found.rows[0].schema,'SJetSchema');assert.equal((await b({command:'cancel',operationId:'unknown'})).cancelled,false);
 }finally{await a({command:'shutdown'});for(const child of clients)child.stdin.end();fs.rmSync(root,{recursive:true,force:true});}
});
test('a held helper lease returns explicit busy instead of hanging another client',async()=>{
 const root=fs.mkdtempSync(path.join(os.tmpdir(),'jade-cli-lease-'));const old=process.env.JADE_TOOLS_STATE_DIRECTORY;process.env.JADE_TOOLS_STATE_DIRECTORY=root;
 const {openClient}=require('../../sdk/broker-client');let a,b;
 try{a=await openClient();b=await openClient();const opened=await a.request({command:'helper.open',kind:'compiler'});assert.equal(opened.passed,true);const blocked=await b.request({command:'build',config:{}});assert.equal(blocked.busy,true);assert.match(blocked.report.error,/owns the runtime/);await b.request({command:'shutdown'});}
 finally{a?.close();b?.close();if(old===undefined)delete process.env.JADE_TOOLS_STATE_DIRECTORY;else process.env.JADE_TOOLS_STATE_DIRECTORY=old;}
});
test('a different packaged engine cannot silently attach to an older broker',async()=>{
 const root=fs.mkdtempSync(path.join(os.tmpdir(),'jade-cli-version-')),old=process.env.JADE_TOOLS_STATE_DIRECTORY;process.env.JADE_TOOLS_STATE_DIRECTORY=path.join(root,'state');
 const {openClient}=require('../../sdk/broker-client');let client;
 try{client=await openClient();const fake=path.join(root,'another-sdk');fs.mkdirSync(path.join(fake,'engine'),{recursive:true});fs.mkdirSync(path.join(fake,'native'));fs.copyFileSync(path.resolve(__dirname,'../../sdk/broker-client.js'),path.join(fake,'broker-client.js'));fs.writeFileSync(path.join(fake,'index.js'),'// Another engine artifact');const other=require(path.join(fake,'broker-client.js'));await assert.rejects(other.openClient(),/different Jade SDK build/);assert.equal((await client.request({command:'doctor'})).passed,true);await client.request({command:'shutdown'});}
 finally{client?.close();if(old===undefined)delete process.env.JADE_TOOLS_STATE_DIRECTORY;else process.env.JADE_TOOLS_STATE_DIRECTORY=old;}
});
