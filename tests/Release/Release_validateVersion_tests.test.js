'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const {validateVersion}=require('../../scripts/prepare-release.cjs');
test('release tag must exactly match a valid package version',()=>{assert.equal(validateVersion('0.1.0','v0.1.0'),'0.1.0');assert.equal(validateVersion('1.2.3-rc.1','v1.2.3-rc.1'),'1.2.3-rc.1');assert.throws(()=>validateVersion('0.1.0','v0.2.0'),/match/);assert.throws(()=>validateVersion('bad'),/version/);});
