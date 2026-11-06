'use strict';
const fs = require('node:fs');
const path = require('node:path');
const { spawn } = require('node:child_process');
const { StringDecoder } = require('node:string_decoder');
const { schemaFromSource } = require('./config');

function resolvePublishConfig(config) {
  const source = config.program;
  if (!source || path.extname(source).toLowerCase() !== '.cls') throw new Error('Open a saved .cls class file to publish.');
  const installPath = config.installPath || process.env.JADE_INSTALL;
  if (!installPath) throw new Error('Set jadeNative.installPath or run Jade: Detect Runtime Settings.');
  const databasePath = config.databasePath || path.join(installPath, 'system');
  const iniPath = config.iniPath || path.join(databasePath, 'jade.ini');
  const executable = path.join(installPath, 'bin', 'jadloadb.exe');
  const server = config.server || 'singleUser';
  if (!['singleUser', 'multiUser'].includes(server)) throw new Error('server must be singleUser or multiUser.');
  for (const [label, file] of [['Jade loader', executable], ['database control file', path.join(databasePath, '_control.dat')],
    ['INI file', iniPath], ['class file', source]]) {
    if (!fs.existsSync(file)) throw new Error(`${label} not found: ${file}`);
  }
  const schema = schemaFromSource(config.sourceText ?? fs.readFileSync(source, 'utf8'));
  if (!schema) throw new Error('The CLS file has no schemaDefinition / subschemaOf header.');
  const args = [`path=${databasePath}`, `ini=${iniPath}`, `server=${server}`, `schemaFile=${source}`,
    `targetSchema=${schema}`, 'loadStyle=currentSchemaVersion', 'replayableReorg=true', 'dontSaveSources=false',
    'compileUnchangedMethods=true', 'ShowProgress=true'];
  const ddx=source.replace(/\.cls$/i,'.ddx');
  if(fs.existsSync(ddx))args.push('ddbFile='+ddx);
  return { executable, args, source, schema, databasePath, iniPath, server };
}

function startPublish(config, onOutput = () => {}) {
  const child = spawn(config.executable, config.args, {
    cwd: path.dirname(config.source), windowsHide: true, shell: false
  });
  const decoders = { stdout: new StringDecoder('utf8'), stderr: new StringDecoder('utf8') };
  for (const category of ['stdout', 'stderr']) {
    child[category].on('data', bytes => onOutput(decoders[category].write(bytes), category));
  }
  const completion = new Promise((resolve, reject) => {
    child.once('error', reject);
    child.once('close', (code, signal) => {
      for (const category of ['stdout', 'stderr']) {
        const tail = decoders[category].end();
        if (tail) onOutput(tail, category);
      }
      resolve({ code: code ?? 1, signal, success: code === 0, publishedWithErrors: code === 8510 });
    });
  });
  return { child, completion };
}

module.exports = { resolvePublishConfig, startPublish };
