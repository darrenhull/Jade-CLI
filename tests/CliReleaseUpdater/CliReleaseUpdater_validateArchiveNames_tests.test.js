'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),os=require('node:os'),path=require('node:path'),crypto=require('node:crypto');
const CliReleaseUpdater=require('../../sdk/automation/CliReleaseUpdater');
test('validateArchiveNames rejects traversal, extra roots and unsafe names',()=>{assert.doesNotThrow(()=>CliReleaseUpdater.validateArchiveNames(['package/package.json','package/sdk/index.js']));for(const name of ['../package.json','package/../bad','/package/file','package/C:/bad','other/file','package\\..\\bad'])assert.throws(()=>CliReleaseUpdater.validateArchiveNames([name]),/archive/i);});
