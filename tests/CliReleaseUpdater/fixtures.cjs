'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),{spawnSync}=require('node:child_process');
module.exports=function fixture(t){
 const root=fs.mkdtempSync(path.join(os.tmpdir(),'jade-update-test-'));t.after(()=>fs.rmSync(root,{recursive:true,force:true}));
 const content=path.join(root,'source'),archive=path.join(root,'release.tgz');
 const names=['jade-cli.exe','bin/jade-cli.js','sdk/index.js','sdk/automation/CliReleaseUpdater.js','sdk/native/jade-test-helper.exe','sdk/native/jade-native-helper.exe','sdk/native/jade-debug-helper.exe','sdk/native/jade-inspect-helper.exe','licenses/node-v24.22.1-LICENSE.txt'];
 for(const name of names){const target=path.join(content,'package',name);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,'fixture');}
 fs.writeFileSync(path.join(content,'package/package.json'),JSON.stringify({name:'jade-tools-cli',version:'0.2.0',bin:{'jade-cli':'bin/jade-cli.js'}}));
 const pack=()=>{const result=spawnSync('tar',['-czf',archive,'-C',content,'package'],{windowsHide:true,encoding:'utf8'});if(result.status!==0)throw Error(result.stderr||'tar fixture failed');return fs.readFileSync(archive);};
 const consumer=path.join(root,'consumer');fs.mkdirSync(path.join(consumer,'packages'),{recursive:true});fs.writeFileSync(path.join(consumer,'package.json'),JSON.stringify({dependencies:{'jade-tools-cli':'file:packages/jade-tools-cli-0.1.0.tgz'}}));fs.writeFileSync(path.join(consumer,'packages/jade-tools-cli-0.1.0.tgz'),'previous archive');
 return {root,content,archive,consumer,pack};
};
