'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),os=require('node:os'),path=require('node:path'),crypto=require('node:crypto');
const CliReleaseUpdater=require('../../sdk/automation/CliReleaseUpdater');
test('verifyChecksum rejects altered release bytes',()=>{const bytes=Buffer.from('archive'),sha=crypto.createHash('sha256').update(bytes).digest('hex');assert.equal(CliReleaseUpdater.verifyChecksum(bytes,sha+'  jade-tools-cli-0.2.0.tgz\n','jade-tools-cli-0.2.0.tgz'),sha);assert.throws(()=>CliReleaseUpdater.verifyChecksum(Buffer.from('modified'),sha+'  jade-tools-cli-0.2.0.tgz','jade-tools-cli-0.2.0.tgz'),/checksum/i);});
