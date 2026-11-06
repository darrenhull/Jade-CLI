#!/usr/bin/env node
'use strict';
const {version}=require('../sdk/package.json');
const fs=require('node:fs'),path=require('node:path'),readline=require('node:readline');
let activeClient;
async function main(){
 const argv=process.argv.slice(2),command=argv.shift();
 if(command==='broker')return require('../sdk/broker').run();
 if(!command||['help','--help','-h'].includes(command)){console.log('jade-cli '+version+'\nCommands: doctor, build, test, tests list, source search, inspect catalog|instances|object|collection, status, disconnect, shutdown\nUsage: jade-cli build [file.cls ...]; jade-cli test <file.cls> [more.cls ...]\nBare build discovers project files and reuses unchanged native inputs.\nAutomatically discovers jade.json in this folder or its parents.\nOptions: --config <json> --workspace <folder> --query <text> --json\nIntegration: service --stdio; helper inspect|debug|compiler [session.json]\nBuild/test use an independent Working database. No publish command is exposed.');return;}
 const {prepareCommand,runCommand}=require('../sdk/cli-arguments');
 let prepared;if(!['service','helper'].includes(command)){let operation=command;if(['tests','source','inspect'].includes(command))operation+='.'+argv.shift();prepared=prepareCommand(operation,argv);}
 const {openClient}=require('../sdk/broker-client');const client=activeClient=await openClient({allowMismatch:command==='shutdown'||command==='status'});
 if(command==='service'){
  const ids=new Map(),active=new Map();client.onEvent(p=>{if(p.event==='output'){const original=ids.get(p.id);if(original!==undefined)process.stdout.write(JSON.stringify({...p,id:original})+'\n');}});
  const rl=readline.createInterface({input:process.stdin,crlfDelay:Infinity});let seq=0;
  rl.on('line',line=>{let request;try{if(Buffer.byteLength(line)>16*1024*1024)throw Error('Service request exceeds limit.');request=JSON.parse(line);if(typeof request.id!=='string'&&typeof request.id!=='number')throw Error('Request id required.');}catch(error){process.stderr.write(error.message+'\n');return;}
   const local=String(++seq);ids.set(local,request.id);if(request.command==='cancel')request.operationId=active.get(String(request.operationId))||request.operationId;else active.set(String(request.id),local);
   client.send({...request,id:local});
  });
  client.onEvent(p=>{if(p.event==='result'){const id=ids.get(p.id);if(id!==undefined){process.stdout.write(JSON.stringify({...p,id})+'\n');ids.delete(p.id);active.delete(String(id));}}});
  rl.on('close',()=>client.close());client.socket.on('close',()=>rl.close());return;
 }
 if(command==='helper'){const kind=argv.shift(),session=argv.shift();if(!['inspect','debug','compiler'].includes(kind))throw Error('Unsupported native helper.');let lease;client.onEvent(p=>{if(p.event==='helper.stdout')process.stdout.write(p.text);else if(p.event==='helper.stderr')process.stderr.write(p.text);else if(p.event==='helper.exit'){process.exitCode=p.code??2;process.stdin.pause();client.close();}});const opened=await client.request({command:'helper.open',kind,session});if(!opened.passed)throw Error(opened.report?.error||'Native helper failed.');lease=opened.lease;process.stdin.setEncoding('utf8');process.stdin.on('data',text=>client.send({id:'write',command:'helper.write',lease,text}));process.stdin.on('end',()=>client.send({id:'end',command:'helper.end',lease}));process.stdin.resume();return;}
 try{const result=await runCommand(prepared,request=>client.request(request,text=>process.stderr.write(text)));console.log(JSON.stringify(result,null,prepared.values.json?0:2));process.exitCode=result.passed===false?result.cancelled?130:1:0;}finally{client.close();}
}
main().catch(error=>{process.stderr.write(error.message+'\n');activeClient?.close();process.stdin.pause();process.exitCode=2;});
