'use strict';
const fs=require('node:fs'),path=require('node:path'),net=require('node:net'),crypto=require('node:crypto'),readline=require('node:readline'),cp=require('node:child_process');
const {prepare,fingerprint}=require('./broker-client');
const {version:sdkVersion}=require('./package.json');
async function run(){
 const state=prepare(),secret=fs.readFileSync(state.token,'utf8');const nativeChildren=new Map(),connections=new Set(),jobs=new Map(),helpers=new Map();let serial=Promise.resolve(),stopping=false,reservedHelper;
 const save=()=>fs.writeFileSync(state.owner,JSON.stringify({brokerPID:process.pid,nativePIDs:[...nativeChildren.keys()],sdkVersion}));
 const originalSpawn=cp.spawn;cp.spawn=function(...args){const child=originalSpawn(...args);if(child.pid){nativeChildren.set(child.pid,child);save();child.once('close',()=>{nativeChildren.delete(child.pid);save();});}return child;};
 // Load native adapters after tracking is installed so a crashed broker cannot
 // silently forget a surviving native database owner.
 const {JadeEngine}=require('./index');const engine=new JadeEngine();
 if(process.platform!=='win32')fs.rmSync(state.pipe,{force:true});
 const server=net.createServer(socket=>{
  connections.add(socket);const owner=crypto.randomUUID(),owned=new Set();let bytes=0;
  const send=packet=>{if(!socket.destroyed)socket.write(JSON.stringify(packet)+'\n');};
  const lines=readline.createInterface({input:socket,crlfDelay:Infinity});
  socket.on('data',chunk=>{bytes+=chunk.length;if(bytes>32*1024*1024)socket.destroy();});
  lines.on('line',line=>{bytes=0;let request;try{request=JSON.parse(line);const supplied=Buffer.from(String(request.token||'')),expected=Buffer.from(secret);if(supplied.length!==expected.length||!crypto.timingSafeEqual(supplied,expected))throw Error('Broker authentication failed.');delete request.token;}catch{socket.destroy();return;}
   const id=String(request.id),key=owner+':'+id;const result=value=>send({id,event:'result',result:value});
   if(request.command==='status'){result({code:0,passed:true,brokerPID:process.pid,nativePIDs:[...nativeChildren.keys()],activeJobs:[...jobs.values()].map(j=>({id:j.id,state:j.state})),sdkVersion,fingerprint:fingerprint()});return;}
   if(request.command!=='shutdown'&&request.fingerprint!==fingerprint()){result({code:2,passed:false,report:{error:'Jade SDK broker artifact fingerprint mismatch. Finish active operations and restart the broker.'}});return;}
   if(request.command==='cancel'){const target=owner+':'+request.operationId,job=jobs.get(target);if(job){job.cancelled=true;engine.cancel(target);}result({code:0,passed:true,cancelled:!!job});return;}
   if(request.command==='helper.write'||request.command==='helper.end'){const h=helpers.get(request.lease);if(!h||h.owner!==owner){result({code:2,passed:false,report:{error:'Native helper lease is not owned by this client.'}});return;}if(request.command==='helper.end')h.child.stdin.end();else if(typeof request.text==='string'&&!h.child.stdin.destroyed)h.child.stdin.write(request.text);return;}
   if(request.command==='shutdown'){for(const job of jobs.values())job.cancelled=true;for(const jobId of jobs.keys())engine.cancel(jobId);for(const h of helpers.values())h.child.kill();}
   if(reservedHelper&&request.command!=='shutdown'){result({code:3,passed:false,busy:true,report:{error:'A native inspection/debug/helper session owns the runtime. Close that session before starting another native operation.'}});return;}
   if(request.command==='helper.open')reservedHelper=key;
   const job={id:key,state:'queued',cancelled:false};jobs.set(key,job);owned.add(key);
   serial=serial.catch(()=>{}).then(async()=>{
    if(job.cancelled||socket.destroyed){jobs.delete(key);owned.delete(key);if(reservedHelper===key)reservedHelper=undefined;result({code:2,passed:false,cancelled:true});return;}
    job.state='running';
    try{
     if(request.command==='shutdown'){stopping=true;await engine.close();for(const h of helpers.values())h.child.kill();result({code:0,passed:true});setTimeout(()=>{for(const c of connections)c.end();server.close();},25);return;}
     if(request.command==='helper.open'){
      const names={inspect:'jade-inspect-helper.exe',debug:'jade-debug-helper.exe',compiler:'jade-native-helper.exe'};if(!names[request.kind])throw Error('Unsupported native helper.');
      let config;if(request.kind!=='compiler'){if(typeof request.session!=='string')throw Error('Native session file required.');config=JSON.parse(fs.readFileSync(request.session,'utf8'));if(!config.installPath||!config.databasePath)throw Error('Incomplete native session configuration.');}
      await engine.release();const child=cp.spawn(path.join(__dirname,'native',names[request.kind]),request.kind==='compiler'?[]:[path.resolve(request.session)],{windowsHide:true,stdio:['pipe','pipe','pipe']});const lease=crypto.randomUUID();helpers.set(lease,{child,owner});
      const decoder={stdout:new (require('node:string_decoder').StringDecoder)('utf8'),stderr:new (require('node:string_decoder').StringDecoder)('utf8')};
      child.stdin.on('error',()=>{});for(const stream of ['stdout','stderr'])child[stream].on('data',b=>send({id,event:'helper.'+stream,text:decoder[stream].write(b)}));
      const finished=new Promise(resolve=>{child.once('error',error=>{send({id,event:'helper.stderr',text:error.message});});child.once('close',code=>{for(const stream of ['stdout','stderr']){const tail=decoder[stream].end();if(tail)send({id,event:'helper.'+stream,text:tail});}helpers.delete(lease);send({id,event:'helper.exit',code});resolve();});});
      result({code:0,passed:true,lease});await finished;return;
     }
     if(request.command?.startsWith('inspect.'))request.sessionId=owner+':'+(request.sessionId||'default');
     const value=await engine.execute(request,text=>send({id,event:'output',text}),key);if(job.cancelled)value.cancelled=true;result(value);
    }catch(error){result({code:2,passed:false,report:{error:error.message}});}finally{jobs.delete(key);owned.delete(key);if(reservedHelper===key)reservedHelper=undefined;}
   });
  });
  socket.on('error',()=>{});socket.on('close',()=>{connections.delete(socket);for(const key of owned){const job=jobs.get(key);if(job){job.cancelled=true;engine.cancel(key);}}for(const h of helpers.values())if(h.owner===owner)h.child.kill();serial=serial.catch(()=>{}).then(()=>engine.releaseClientInspections(owner+':'));});
 });
 await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(state.pipe,resolve);});save();if(process.platform!=='win32')fs.chmodSync(state.pipe,0o600);
 const close=async()=>{if(stopping)return;stopping=true;for(const id of jobs.keys())engine.cancel(id);await engine.close();for(const child of nativeChildren.values())child.kill();for(const socket of connections)socket.end();server.close();};
 process.on('SIGTERM',()=>void close());process.on('SIGINT',()=>void close());server.on('close',()=>{cp.spawn=originalSpawn;save();});
}
module.exports={run};
