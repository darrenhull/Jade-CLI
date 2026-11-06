'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const {prepareCommand,runCommand}=require('../../sdk/cli-arguments');
const {fixture}=require('../fixtures/cli-project.cjs');
test('discovers nearest parent jade.json and resolves config paths relative to it',t=>{const root=fixture(t),p=prepareCommand('build',['B.cls'],path.join(root,'src'));assert.equal(p.config.installPath,path.join(root,'runtime'));assert.deepEqual(p.config.workspaceRoots,[root]);assert.deepEqual(p.files,[path.join(root,'src/B.cls')]);});
test('explicit config overrides discovery and switches do not consume positional files',t=>{const root=fixture(t);fs.writeFileSync(path.join(root,'other.json'),'{}');const p=prepareCommand('build',['--json','src/A.cls','--config','other.json','src/B.cls'],root);assert.equal(p.configFile,path.join(root,'other.json'));assert.equal(p.files.length,2);assert.equal(p.values.json,true);});
test('rejects missing files and missing configuration before connecting',t=>{const root=fixture(t);assert.throws(()=>prepareCommand('build',['src/missing.cls'],root),/not a file/i);assert.throws(()=>prepareCommand('test',[],os.tmpdir()),/jade.json/i);});
