'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),os=require('node:os'),path=require('node:path'),crypto=require('node:crypto');
const CliReleaseUpdater=require('../../sdk/automation/CliReleaseUpdater');
test('shouldUpdate allows a repacked same-version release but never downgrades',()=>{assert.equal(CliReleaseUpdater.shouldUpdate('0.1.0','a','0.2.0','a'),true);assert.equal(CliReleaseUpdater.shouldUpdate('0.2.0','a','0.1.0','b'),false);assert.equal(CliReleaseUpdater.shouldUpdate('0.2.0','a','0.2.0','a'),false);assert.equal(CliReleaseUpdater.shouldUpdate('0.2.0','a','0.2.0','b'),true);});
