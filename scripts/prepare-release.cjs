'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
function validateVersion(version,tag){
 if(typeof version!=='string'||!/^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?$/.test(version))throw Error('Invalid release package version.');
 if(tag!==undefined&&tag!=='v'+version)throw Error('Release tag must exactly match package.json: v'+version+'.');
 return version;
}
function prepareRelease(root,tag){
 const manifest=JSON.parse(fs.readFileSync(path.join(root,'package.json'),'utf8'));
 const version=validateVersion(manifest.version,tag),dist=path.join(root,'dist'),folder=path.join(dist,'jade-cli-win-x64');
 const distribution=JSON.parse(fs.readFileSync(path.join(folder,'distribution.json'),'utf8'));
 if(distribution.platform!=='win32'||distribution.arch!=='x64'||distribution.packageVersion!==version)throw Error('Standalone distribution version or platform does not match the release.');
 const executable=path.join(folder,'jade-cli.exe');
 if(sha(executable)!==distribution.executableSha256||sha(path.join(root,'jade-cli.exe'))!==distribution.executableSha256)throw Error('Release executable does not match the standalone distribution.');
 if(!distribution.runtimeFiles||!Object.keys(distribution.runtimeFiles).length)throw Error('Standalone runtime manifest is empty.');
 for(const [relative,expected]of Object.entries(distribution.runtimeFiles)){
  const file=path.resolve(folder,relative),inside=path.relative(folder,file);
  if(!relative||inside==='..'||inside.startsWith('..'+path.sep)||path.isAbsolute(inside)||path.win32.isAbsolute(relative)||relative.includes('\\')||relative.split('/').some(part=>!part||part==='.'||part==='..'))throw Error('Unsafe runtime manifest path.');
  const real=path.relative(fs.realpathSync(folder),fs.realpathSync(file));
  if(real==='..'||real.startsWith('..'+path.sep)||path.isAbsolute(real)||!fs.statSync(file).isFile())throw Error('Unsafe runtime manifest path.');
  if(sha(file)!==expected)throw Error('Runtime file hash mismatch: '+relative);
 }
 const assets=['jade-cli-win-x64.zip','jade-cli.exe','jade-tools-cli-'+version+'.tgz'];
 for(const name of [assets[0],assets[2]])if(!fs.statSync(path.join(dist,name)).isFile()||fs.statSync(path.join(dist,name)).size===0)throw Error('Missing release asset: '+name);
 fs.copyFileSync(executable,path.join(dist,'jade-cli.exe'));
 fs.writeFileSync(path.join(dist,'SHA256SUMS'),assets.map(name=>sha(path.join(dist,name))+'  '+name).join('\n')+'\n');
 return {version,tag:'v'+version,prerelease:version.includes('-'),assets};
}
if(require.main===module){try{const result=prepareRelease(path.resolve(__dirname,'..'),process.env.GITHUB_REF_TYPE==='tag'?process.env.GITHUB_REF_NAME:undefined);console.log(JSON.stringify(result,null,2));}catch(error){console.error(error.message);process.exitCode=1;}}
module.exports={prepareRelease,validateVersion};
