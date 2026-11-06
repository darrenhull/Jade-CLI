'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const CliReleaseUpdater=require('../../sdk/automation/CliReleaseUpdater'),fixture=require('./fixtures.cjs');
test('validateArchive checks the complete package without extracting files',t=>{const f=fixture(t);f.pack();assert.equal(CliReleaseUpdater.validateArchive(f.archive,'0.2.0').version,'0.2.0');assert.throws(()=>CliReleaseUpdater.validateArchive(f.archive,'9.0.0'),/metadata/i);});
test('validateArchive rejects links before a dependency can be installed',t=>{const f=fixture(t);fs.linkSync(path.join(f.content,'package/jade-cli.exe'),path.join(f.content,'package/linked.exe'));f.pack();assert.throws(()=>CliReleaseUpdater.validateArchive(f.archive,'0.2.0'),/links/i);});
