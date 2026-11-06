'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const {prepareCommand,runCommand}=require('../../sdk/cli-arguments');
const {fixture}=require('../fixtures/cli-project.cjs');

test('selected tests include unpublished workspace classes as dependency candidates without running them as tests',async t=>{
 const root=fixture(t),domain=path.join(root,'src/UnpublishedFish.cls');
 fs.writeFileSync(domain,'source');
 const prepared=prepareCommand('test',['src/A.cls','src/B.cls'],root),requests=[];
 await runCommand(prepared,async request=>{requests.push(request);return {passed:true,code:0};});
 assert.deepEqual(requests.map(r=>r.config.program),prepared.files);
 for(const request of requests)assert.ok(request.config.files.includes(domain),'Local referenced classes must be available to native dependency selection.');
});
test('multiple files share dependency inputs, run sequentially and preserve per-file failures',async t=>{const root=fixture(t),p=prepareCommand('test',['src/A.cls','src/B.cls','src/A.cls'],root),seen=[];const result=await runCommand(p,async request=>{seen.push(request);return {passed:seen.length!==1,code:seen.length===1?6026:0};});assert.equal(seen.length,2);assert.deepEqual(seen[0].config.files,p.files);assert.equal(seen[1].config.program,p.files[1]);assert.equal(result.passed,false);assert.equal(result.results[0].result.code,6026);});
test('single-file response is unchanged',async t=>{const root=fixture(t),p=prepareCommand('build',['src/A.cls'],root);const result={code:0,passed:true};assert.equal(await runCommand(p,async r=>{assert.equal(r.config.program,path.join(root,'src/A.cls'));return result;}),result);});
test('multiple build files are passed to one native project build',async t=>{const root=fixture(t),p=prepareCommand('build',['src/A.cls','src/B.cls'],root);let calls=0;await runCommand(p,async r=>{calls++;assert.deepEqual(r.config.files,p.files);return {code:0,passed:true};});assert.equal(calls,1);});
test('bare build discovers all project inputs instead of a legacy selected program and rescans new files',async t=>{const root=fixture(t);let seen;const execute=async r=>{seen=r;return {passed:true,code:0};};await runCommand(prepareCommand('build',[],path.join(root,'src')),execute);assert.deepEqual(seen.config.files,[path.join(root,'src/A.cls'),path.join(root,'src/B.cls')]);fs.writeFileSync(path.join(root,'src/C.cls'),'source');await runCommand(prepareCommand('build',[],root),execute);assert.equal(seen.config.files.length,3);});
test('bare build of an empty project fails before native execution',async t=>{const root=fixture(t);for(const name of ['A','B'])fs.unlinkSync(path.join(root,'src',name+'.cls'));await assert.rejects(runCommand(prepareCommand('build',[],root),()=>assert.fail('must not connect')),/no Jade source/i);});
