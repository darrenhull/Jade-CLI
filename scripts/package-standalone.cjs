'use strict';
// Official Node SEA resource injection; no new compiler/runtime emulation.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),{spawnSync}=require('node:child_process');
async function main(){
 if(process.platform!=='win32'||process.arch!=='x64')throw Error('The initial standalone distribution targets Windows x64.');
 if(process.version!=='v24.22.1')throw Error('Use the pinned Node v24.22.1 packaging runtime and its matching license.');
 const root=path.resolve(__dirname,'..'),output=path.join(root,'dist/jade-cli-win-x64'),build=path.join(root,'build/sea');
 if(path.relative(path.join(root,'dist'),output)!=='jade-cli-win-x64')throw Error('Unsafe distribution output path.');
 fs.rmSync(output,{recursive:true,force:true});
 fs.mkdirSync(output,{recursive:true});fs.mkdirSync(build,{recursive:true});
 const runtime=output;
 for(const folder of ['bin','sdk'])fs.cpSync(path.join(root,folder),path.join(runtime,folder),{recursive:true});
 for(const file of ['README.md','ThirdPartyNotices.txt'])fs.copyFileSync(path.join(root,file),path.join(output,file));
 fs.copyFileSync(path.join(root,'licenses/node-v24.22.1-LICENSE.txt'),path.join(output,'NODE-LICENSE.txt'));
 const bootstrap=path.join(build,'bootstrap.cjs');
 fs.writeFileSync(bootstrap,`'use strict';\nconst path=require('node:path');\nconst entry=path.join(path.dirname(process.execPath),'bin','jade-cli.js');\n// SDK broker self-spawns the current executable with this exact entry path.\nif(process.argv[2]&&path.resolve(process.argv[2]).toLowerCase()===entry.toLowerCase())process.argv.splice(2,1);\nprocess.argv[1]=entry;\nrequire('node:module').createRequire(entry)(entry);\n`);
 const blob=path.join(build,'jade-cli.blob'),config=path.join(build,'sea-config.json');
 fs.writeFileSync(config,JSON.stringify({main:bootstrap,output:blob,disableExperimentalSEAWarning:true,useSnapshot:false,useCodeCache:false,execArgvExtension:'none'}));
 const prepared=spawnSync(process.execPath,['--experimental-sea-config',config],{encoding:'utf8',windowsHide:true});if(prepared.status!==0)throw Error(prepared.stderr||'Node SEA preparation failed.');
 const executable=path.join(output,'jade-cli.exe');fs.copyFileSync(process.execPath,executable);
 const kits=path.join(process.env['ProgramFiles(x86)']||'C:/Program Files (x86)','Windows Kits/10/bin');
 const signTool=process.env.SIGNTOOL||(fs.existsSync(kits)?fs.readdirSync(kits).sort().reverse().map(v=>path.join(kits,v,'x64/signtool.exe')).find(p=>fs.existsSync(p)):undefined);
 if(signTool){const removed=spawnSync(signTool,['remove','/s',executable],{encoding:'utf8',windowsHide:true});if(removed.status!==0)throw Error(removed.stderr||'Could not remove the original Node executable signature.');}
 await require('postject').inject(executable,'NODE_SEA_BLOB',fs.readFileSync(blob),{sentinelFuse:'NODE_SEA_FUSE_fce680ab2cc467b6e072b8b5df1996b2'});
 const sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
 const manifest={formatVersion:1,platform:'win32',arch:'x64',nodeVersion:process.version,packageVersion:require('../package.json').version,executable:'jade-cli.exe',executableSha256:sha(executable),runtimeFiles:{}};
 function collect(directory){for(const item of fs.readdirSync(directory,{withFileTypes:true})){const file=path.join(directory,item.name);if(item.isDirectory())collect(file);else manifest.runtimeFiles[path.relative(output,file).replaceAll('\\','/')]=sha(file);}}for(const folder of ['bin','sdk'])collect(path.join(runtime,folder));
 fs.writeFileSync(path.join(output,'distribution.json'),JSON.stringify(manifest,null,2)+'\n');
 fs.copyFileSync(executable,path.join(root,'jade-cli.exe'));
 const zip=path.join(root,'dist/jade-cli-win-x64.zip'),zipScript=path.join(build,'archive.ps1');
 fs.writeFileSync(zipScript,"param([string]$Source,[string]$Destination)\n$ErrorActionPreference='Stop'\nCompress-Archive -Path (Join-Path $Source '*') -DestinationPath $Destination -Force\n");
 const archived=spawnSync(path.join(process.env.SystemRoot||'C:/Windows','System32/WindowsPowerShell/v1.0/powershell.exe'),['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',zipScript,output,zip],{encoding:'utf8',windowsHide:true});
 if(archived.status!==0)throw Error(archived.stderr||'Could not create the standalone distribution archive.');
 console.log('Standalone CLI: '+executable+'\nDistribute this complete folder; no Node installation is required.');
 console.log('Archive: '+zip);
}
main().catch(error=>{console.error(error.message);process.exitCode=1;});
