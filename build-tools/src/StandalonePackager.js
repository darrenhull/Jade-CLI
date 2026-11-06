'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),{spawnSync}=require('node:child_process');

/** Development-only Node SEA packaging for the JavaScript MCP adapter.
 * The native Jade CLI is built separately in C++.
 */
class StandalonePackager {
  constructor(options){this.options=options;}
  plan(){
    const o=this.options,root=path.resolve(o.root);
    if(!/^[a-z0-9][a-z0-9-]*$/i.test(o.distributionName||''))throw Error('Invalid distribution name.');
    if(!/^[a-z0-9][a-z0-9-]*\.exe$/i.test(o.executableName||''))throw Error('Invalid executable name.');
    const relative=(value,label)=>{if(typeof value!=='string'||!value||path.isAbsolute(value)||value.split(/[\\/]/).includes('..'))throw Error(label+' must be a package-relative path.');return path.resolve(root,value);};
    const output=path.join(root,'dist',o.distributionName),build=path.join(root,'build','sea-'+o.distributionName);
    for(const destination of [output,build]){let parent=path.dirname(destination);while(!fs.existsSync(parent))parent=path.dirname(parent);const real=path.relative(fs.realpathSync(root),fs.realpathSync(parent));if(real==='..'||real.startsWith('..'+path.sep)||path.isAbsolute(real))throw Error('Distribution output resolves outside the package root.');}
    const folders=o.runtimeFolders||[],files=o.runtimeFiles||[];
    for(const folder of folders){const resolved=relative(folder,'Runtime folder');if(['dist','build'].includes(folder.split(/[\\/]/)[0])||!fs.existsSync(resolved)||!fs.statSync(resolved).isDirectory())throw Error('Invalid runtime folder or output recursion: '+folder);}
    for(const file of files)if(!fs.existsSync(relative(file,'Runtime file')))throw Error('Missing runtime file: '+file);
    const entry=relative(o.entrypoint,'Entrypoint');
    if(!fs.existsSync(entry)||!fs.statSync(entry).isFile()||!files.includes(o.entrypoint)&&!folders.some(folder=>o.entrypoint.replaceAll('\\','/').startsWith(folder.replaceAll('\\','/')+'/')))throw Error('Entrypoint must be a shipped runtime input.');
    const license=relative(o.nodeLicense,'Node license');if(!fs.existsSync(license))throw Error('Missing Node runtime license.');
    if(o.brokerEntrypoint){
      const broker=relative(o.brokerEntrypoint,'Broker entrypoint');
      if(!fs.existsSync(broker)||!fs.statSync(broker).isFile()||!files.includes(o.brokerEntrypoint)&&!folders.some(folder=>o.brokerEntrypoint.replaceAll('\\','/').startsWith(folder.replaceAll('\\','/')+'/')))throw Error('Broker entrypoint must be a shipped runtime input.');
    }
    return {root,output,build,zip:path.join(root,'dist',o.distributionName+'.zip'),executable:path.join(output,o.executableName),entrypoint:path.join(output,o.entrypoint),license};
  }
  async build({inject,run=spawnSync,nodeExecutable=process.execPath,nodeVersion=process.version,platform=process.platform,arch=process.arch}={}){
    if(platform!=='win32'||arch!=='x64')throw Error('Standalone distributions require Windows x64.');
    if(nodeVersion!=='v24.22.1')throw Error('Use the pinned Node v24.22.1 packaging runtime and matching license.');
    if(typeof inject!=='function')throw Error('Supply the pinned official postject injection function.');
    const o=this.options,p=this.plan();
    fs.rmSync(p.output,{recursive:true,force:true});fs.mkdirSync(p.output,{recursive:true});fs.mkdirSync(p.build,{recursive:true});
    const filter=o.copyFilter?(source=>o.copyFilter(path.relative(p.root,source).replaceAll('\\','/'))):undefined;
    for(const folder of o.runtimeFolders)fs.cpSync(path.join(p.root,folder),path.join(p.output,folder),{recursive:true,filter});
    for(const file of o.runtimeFiles){const target=path.join(p.output,file);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(path.join(p.root,file),target);}
    fs.copyFileSync(p.license,path.join(p.output,'NODE-LICENSE.txt'));
    const bootstrap=path.join(p.build,'bootstrap.cjs'),blob=path.join(p.build,'application.blob'),config=path.join(p.build,'sea-config.json');
    const entry=JSON.stringify(o.entrypoint),broker=JSON.stringify(o.brokerEntrypoint||o.entrypoint);
    const load=o.moduleType==='module'?"import(require('node:url').pathToFileURL(entry).href).catch(error=>{console.error(error.message);process.exitCode=1;});":"require('node:module').createRequire(entry)(entry);";
    fs.writeFileSync(bootstrap,`'use strict';\nconst path=require('node:path'),root=path.dirname(process.execPath),entry=path.join(root,${entry}),broker=path.join(root,${broker});\nif(process.argv[2]&&path.resolve(process.argv[2]).toLowerCase()===broker.toLowerCase()){process.argv.splice(2,1);process.argv[1]=broker;require('node:module').createRequire(broker)(broker);}else{process.argv[1]=entry;${load}}\n`);
    fs.writeFileSync(config,JSON.stringify({main:bootstrap,output:blob,disableExperimentalSEAWarning:true,useSnapshot:false,useCodeCache:false,execArgvExtension:'none'}));
    const prepared=run(nodeExecutable,['--experimental-sea-config',config],{encoding:'utf8',windowsHide:true});if(prepared.status!==0)throw Error(prepared.stderr||'Node SEA preparation failed.');
    fs.copyFileSync(nodeExecutable,p.executable);
    const kits=path.join(process.env['ProgramFiles(x86)']||'C:/Program Files (x86)','Windows Kits/10/bin');
    const signTool=process.env.SIGNTOOL||(fs.existsSync(kits)?fs.readdirSync(kits).sort().reverse().map(version=>path.join(kits,version,'x64/signtool.exe')).find(file=>fs.existsSync(file)):undefined);
    if(signTool){const removed=run(signTool,['remove','/s',p.executable],{encoding:'utf8',windowsHide:true});if(removed.status!==0)throw Error(removed.stderr||'Could not remove the original Node executable signature.');}
    await inject(p.executable,'NODE_SEA_BLOB',fs.readFileSync(blob),{sentinelFuse:'NODE_SEA_FUSE_fce680ab2cc467b6e072b8b5df1996b2'});
    const sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex'),runtimeFiles={};
    const collect=directory=>{for(const entry of fs.readdirSync(directory,{withFileTypes:true})){const file=path.join(directory,entry.name);if(entry.isDirectory())collect(file);else if(file!==p.executable)runtimeFiles[path.relative(p.output,file).replaceAll('\\','/')]=sha(file);}};collect(p.output);
    const manifest={formatVersion:1,platform,arch,nodeVersion,packageVersion:JSON.parse(fs.readFileSync(path.join(p.root,'package.json'),'utf8')).version,executable:o.executableName,executableSha256:sha(p.executable),runtimeFiles};
    fs.writeFileSync(path.join(p.output,'distribution.json'),JSON.stringify(manifest,null,2)+'\n');
    fs.copyFileSync(p.executable,path.join(p.root,o.executableName));
    const archiveScript=path.join(p.build,'archive.ps1');fs.writeFileSync(archiveScript,"param([string]$Source,[string]$Destination)\n$ErrorActionPreference='Stop'\nCompress-Archive -Path (Join-Path $Source '*') -DestinationPath $Destination -Force\n");
    const archived=run(path.join(process.env.SystemRoot||'C:/Windows','System32/WindowsPowerShell/v1.0/powershell.exe'),['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',archiveScript,p.output,p.zip],{encoding:'utf8',windowsHide:true});if(archived.status!==0)throw Error(archived.stderr||'Could not create the standalone ZIP.');
    console.log('Standalone application: '+p.executable+'\nDistribute the complete folder; no Node installation is required.\nArchive: '+p.zip);
    return {...p,manifest};
  }
}
module.exports={StandalonePackager};
