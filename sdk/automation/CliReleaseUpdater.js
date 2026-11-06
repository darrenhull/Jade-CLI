'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),crypto=require('node:crypto'),{spawnSync}=require('node:child_process');

// Shared build-time dependency updater for the extension and MCP repositories.
// It never runs the downloaded CLI or connects to a Jade database.
class CliReleaseUpdater {
  constructor({root,fetch:fetchRelease=globalThis.fetch}={}) {
    this.root=path.resolve(root||process.cwd());
    this.fetch=fetchRelease;
  }

  static releaseAsset(release) {
    if(release.draft||release.prerelease||!/^v\d+\.\d+\.\d+$/.test(release.tag_name||''))throw Error('Select a stable CLI release tagged v<major>.<minor>.<patch>.');
    const version=release.tag_name.slice(1),filename='jade-tools-cli-'+version+'.tgz';
    const asset=name=>{
      const rows=(release.assets||[]).filter(item=>item.name===name);
      if(rows.length!==1)throw Error('CLI release is missing asset '+name+'.');
      const expected='https://github.com/darrenhull/Jade-CLI/releases/download/'+release.tag_name+'/'+name;
      if(rows[0].browser_download_url!==expected)throw Error('CLI assets must use the official Jade-CLI release URL.');
      return rows[0].browser_download_url;
    };
    return {tag:release.tag_name,version,filename,url:asset(filename),checksumsUrl:asset('SHA256SUMS')};
  }

  static verifyChecksum(bytes,manifest,filename) {
    const matches=String(manifest).split(/\r?\n/).map(line=>/^([a-fA-F0-9]{64}) [ *](\S+)$/.exec(line)).filter(row=>row&&row[2]===filename);
    const actual=crypto.createHash('sha256').update(bytes).digest('hex');
    if(matches.length!==1||matches[0][1].toLowerCase()!==actual)throw Error('CLI release checksum verification failed.');
    return actual;
  }

  static validateArchiveNames(names) {
    if(!names.length||names.length>5000)throw Error('Unexpected CLI archive size.');
    for(const name of names){
      const parts=name.replace(/\/$/,'').split('/');
      if(name.length>512||parts[0]!=='package'||parts.some(part=>!part||part==='.'||part==='..')||!/^[A-Za-z0-9_@./-]+$/.test(name))throw Error('Unsafe CLI archive path.');
    }
  }

  static shouldUpdate(currentVersion,currentSha,nextVersion,nextSha) {
    const current=currentVersion.split('.').map(Number),next=nextVersion.split('.').map(Number);
    for(let i=0;i<3;i++){if(next[i]>current[i])return true;if(next[i]<current[i])return false;}
    return currentSha!==nextSha;
  }

  static pinned(root) {
    const manifest=JSON.parse(fs.readFileSync(path.join(root,'package.json'),'utf8'));
    const pin=manifest.dependencies?.['jade-tools-cli'],matched=/^file:packages\/(jade-tools-cli-(\d+\.\d+\.\d+)\.tgz)$/.exec(pin||'');
    if(!matched)throw Error('Expected a pinned file:packages/jade-tools-cli-<version>.tgz dependency.');
    const file=path.join(root,'packages',matched[1]);
    return {manifest,file,filename:matched[1],version:matched[2],sha:crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex')};
  }

  static validateArchive(file,version) {
    const tar=process.platform==='win32'?path.join(process.env.SystemRoot||'C:/Windows','System32/tar.exe'):'/usr/bin/tar';
    const run=args=>{const result=spawnSync(tar,args,{encoding:'utf8',windowsHide:true,timeout:30000,maxBuffer:4*1024*1024});if(result.error||result.status!==0)throw Error('Cannot inspect CLI release archive: '+(result.error?.message||result.stderr));return result.stdout;};
    const names=run(['-tzf',file]).trim().split(/\r?\n/);
    this.validateArchiveNames(names);
    if(run(['-tvzf',file]).split(/\r?\n/).filter(Boolean).some(line=>!/^[-d]/.test(line)||/ -> | link to /.test(line)))throw Error('CLI archive links and special files are not allowed.');
    for(const name of ['package/package.json','package/jade-cli.exe','package/bin/jade-cli.js','package/sdk/index.js','package/sdk/automation/CliReleaseUpdater.js','package/sdk/native/jade-test-helper.exe','package/licenses/node-v24.22.1-LICENSE.txt'])if(!names.includes(name))throw Error('CLI archive is missing '+name+'.');
    const manifest=JSON.parse(run(['-xOzf',file,'package/package.json']));
    if(manifest.name!=='jade-tools-cli'||manifest.version!==version||manifest.bin?.['jade-cli']!=='bin/jade-cli.js')throw Error('CLI archive package metadata does not match its release version.');
    return manifest;
  }

  static verifyLock(root) {
    const pin=this.pinned(root),lock=JSON.parse(fs.readFileSync(path.join(root,'package-lock.json'),'utf8'));
    const dependency='file:packages/'+pin.filename,entry=lock.packages?.['node_modules/jade-tools-cli'];
    const integrity='sha512-'+crypto.createHash('sha512').update(fs.readFileSync(pin.file)).digest('base64');
    if(lock.packages?.['']?.dependencies?.['jade-tools-cli']!==dependency||entry?.version!==pin.version||entry?.resolved!==dependency||entry?.integrity!==integrity)throw Error('CLI lockfile version or integrity does not match the committed archive.');
    return true;
  }

  static updatePin(root,release,bytes) {
    if(!/^\d+\.\d+\.\d+$/.test(release.version)||release.filename!=='jade-tools-cli-'+release.version+'.tgz')throw Error('Unsafe CLI package filename.');
    const current=this.pinned(root),folder=path.join(root,'packages'),destination=path.join(folder,release.filename);
    fs.writeFileSync(destination,bytes);
    current.manifest.dependencies['jade-tools-cli']='file:packages/'+release.filename;
    fs.writeFileSync(path.join(root,'package.json'),JSON.stringify(current.manifest,null,2)+'\n');
    fs.writeFileSync(path.join(folder,'README.md'),'# Pinned Jade CLI release\n\n`'+release.filename+'` is the exact reviewed CLI dependency committed with this consumer. No sibling checkout or separately installed Node runtime is needed by end users.\n\nSHA-256: `'+release.sha+'`.\n\nThe archive contains the portable CLI, shared SDK, owned native helpers, source and license notices. Jade vendor DLLs, databases and private configuration are excluded. Keep this archive and package-lock.json together when upgrading.\n');
    if(current.file!==destination){
      const relative=path.relative(root,current.file).split(path.sep).join('/');
      const tracked=spawnSync('git',['ls-files','--error-unmatch','--',relative],{cwd:root,windowsHide:true,stdio:'ignore'});
      if(tracked.status===0)fs.unlinkSync(current.file);
    }
  }

  async read(url,maximum) {
    const headers={'Accept':'application/vnd.github+json','User-Agent':'jade-cli-dependency-updater'};
    if(url.startsWith('https://api.github.com/')&&process.env.GH_TOKEN)headers.Authorization='Bearer '+process.env.GH_TOKEN;
    const response=await this.fetch(url,{headers,signal:AbortSignal.timeout(120000)});
    if(!response.ok)throw Error('GitHub release request failed ('+response.status+').');
    if(Number(response.headers.get('content-length'))>maximum)throw Error('CLI release asset exceeds the size limit.');
    const chunks=[];let size=0;
    for await(const chunk of response.body){size+=chunk.length;if(size>maximum)throw Error('CLI release asset exceeds the size limit.');chunks.push(chunk);}
    return Buffer.concat(chunks);
  }

  async run(tag='') {
    if(tag&&!/^v\d+\.\d+\.\d+$/.test(tag))throw Error('CLI tag must be v<major>.<minor>.<patch>.');
    const endpoint=tag?'tags/'+tag:'latest';
    const release=CliReleaseUpdater.releaseAsset(JSON.parse((await this.read('https://api.github.com/repos/darrenhull/Jade-CLI/releases/'+endpoint,1024*1024)).toString('utf8')));
    const current=CliReleaseUpdater.pinned(this.root);
    if(!CliReleaseUpdater.shouldUpdate(current.version,current.sha,release.version,'not-downloaded')&&current.version!==release.version)return {updated:false,reason:'The selected CLI release is older than the pin.'};
    const [bytes,checksums]=await Promise.all([this.read(release.url,256*1024*1024),this.read(release.checksumsUrl,1024*1024)]);
    release.sha=CliReleaseUpdater.verifyChecksum(bytes,checksums.toString('utf8'),release.filename);
    if(!CliReleaseUpdater.shouldUpdate(current.version,current.sha,release.version,release.sha))return {updated:false,tag:release.tag,reason:'The CLI release is already pinned.'};
    const temporary=path.join(os.tmpdir(),'jade-cli-release-'+crypto.randomUUID()+'.tgz');
    fs.writeFileSync(temporary,bytes,{flag:'wx'});
    try { CliReleaseUpdater.validateArchive(temporary,release.version); }
    finally { fs.unlinkSync(temporary); }
    CliReleaseUpdater.updatePin(this.root,release,bytes);
    return {updated:true,tag:release.tag,version:release.version,sha:release.sha,branch:'codex/cli-'+release.tag+'-'+release.sha.slice(0,12)};
  }
}

module.exports=CliReleaseUpdater;
