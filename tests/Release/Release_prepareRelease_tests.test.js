'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),os=require('node:os'),path=require('node:path'),crypto=require('node:crypto');
const {prepareRelease}=require('../../scripts/prepare-release.cjs');
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
function fixture(t){const root=fs.mkdtempSync(path.join(os.tmpdir(),'jade-release-test-'));t.after(()=>fs.rmSync(root,{recursive:true,force:true}));const folder=path.join(root,'dist/jade-cli-win-x64');fs.mkdirSync(path.join(folder,'bin'),{recursive:true});fs.writeFileSync(path.join(root,'package.json'),JSON.stringify({name:'jade-tools-cli',version:'0.1.0'}));const exe=Buffer.from('MZ fixture executable'),entry=Buffer.from('fixture runtime');fs.writeFileSync(path.join(folder,'jade-cli.exe'),exe);fs.writeFileSync(path.join(root,'jade-cli.exe'),exe);fs.writeFileSync(path.join(folder,'bin/jade-cli.js'),entry);fs.writeFileSync(path.join(folder,'distribution.json'),JSON.stringify({platform:'win32',arch:'x64',packageVersion:'0.1.0',executableSha256:sha(exe),runtimeFiles:{'bin/jade-cli.js':sha(entry)}}));for(const file of ['jade-cli-win-x64.zip','jade-tools-cli-0.1.0.tgz'])fs.writeFileSync(path.join(root,'dist',file),'fixture archive');return {root,folder};}
test('release preparation checks runtime files and emits standard SHA256SUMS for all assets',t=>{const {root}=fixture(t),result=prepareRelease(root,'v0.1.0');assert.equal(result.version,'0.1.0');assert.deepEqual(result.assets,['jade-cli-win-x64.zip','jade-cli.exe','jade-tools-cli-0.1.0.tgz']);const lines=fs.readFileSync(path.join(root,'dist/SHA256SUMS'),'utf8').trim().split('\n');assert.equal(lines.length,3);for(const name of result.assets)assert.ok(lines.includes(sha(fs.readFileSync(path.join(root,'dist',name)))+'  '+name));});
test('release preparation fails closed for modified or missing runtime assets',t=>{const {root,folder}=fixture(t);fs.writeFileSync(path.join(folder,'bin/jade-cli.js'),'modified');assert.throws(()=>prepareRelease(root),/runtime.*hash/i);assert.equal(fs.existsSync(path.join(root,'dist/SHA256SUMS')),false);});
test('release preparation rejects a mismatched executable and unsafe manifest path',t=>{const {root,folder}=fixture(t);fs.writeFileSync(path.join(root,'jade-cli.exe'),'another');assert.throws(()=>prepareRelease(root),/executable.*match/i);fs.copyFileSync(path.join(folder,'jade-cli.exe'),path.join(root,'jade-cli.exe'));const file=path.join(folder,'distribution.json'),manifest=JSON.parse(fs.readFileSync(file));manifest.runtimeFiles={'../escape':'bad'};fs.writeFileSync(file,JSON.stringify(manifest));assert.throws(()=>prepareRelease(root),/runtime.*path/i);});
test('release preparation does not publish checksums for an incomplete archive set',t=>{const {root}=fixture(t);fs.unlinkSync(path.join(root,'dist/jade-tools-cli-0.1.0.tgz'));assert.throws(()=>prepareRelease(root),/ENOENT/);assert.equal(fs.existsSync(path.join(root,'dist/SHA256SUMS')),false);});

test('release preparation verifies shipped documentation and licenses alongside executable sidecars',t=>{
 const {root,folder}=fixture(t),manifestPath=path.join(folder,'distribution.json');
 const manifest=JSON.parse(fs.readFileSync(manifestPath,'utf8'));
 for(const relative of ['README.md','NODE-LICENSE.txt','docs/architecture.svg']){
  const file=path.join(folder,relative);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,'shipped documentation');
  manifest.runtimeFiles[relative]=sha(fs.readFileSync(file));
 }
 fs.writeFileSync(manifestPath,JSON.stringify(manifest));
 assert.equal(prepareRelease(root).version,'0.1.0');
 fs.writeFileSync(path.join(folder,'docs/architecture.svg'),'altered');
 assert.throws(()=>prepareRelease(root),/runtime.*hash/i);
});
