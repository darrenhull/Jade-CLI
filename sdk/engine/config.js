'use strict';
const fs = require('node:fs');
const path = require('node:path');

function parseIni(text) {
  const result = {};
  let section = '';
  for (const raw of text.replace(/^\uFEFF/, '').split(/\r?\n/)) {
    const line = raw.trim();
    if (!line || /^[;#]/.test(line)) continue;
    const header = /^\[([^\]]+)\]$/.exec(line);
    if (header) { section = header[1].toLowerCase(); result[section] ||= {}; continue; }
    const equal = line.indexOf('=');
    if (equal >= 0) (result[section] ||= {})[line.slice(0, equal).trim().toLowerCase()] = line.slice(equal + 1).trim();
  }
  return result;
}

function readIni(file) {
  const bytes = fs.readFileSync(file);
  return parseIni(bytes[0] === 0xff && bytes[1] === 0xfe ? bytes.toString('utf16le') : bytes.toString('utf8'));
}

function schemaFromSource(text) {
  return /^\s*([A-Za-z_]\w*)\s+subschemaOf\s+([A-Za-z_]\w*)\b/m.exec(text)?.[1];
}

function hasMethodBodies(source) {
  const section = /^\s*typeSources\s*$/m.exec(source);
  return Boolean(section && /^\w+\s*\r?\n\{\r?\n[\s\S]*?^\}/m.test(source.slice(section.index + section[0].length)));
}

function inspectInstall(directory) {
  const installPath = path.resolve(directory);
  if (!fs.existsSync(path.join(installPath, 'bin', 'jom.dll'))) throw new Error(`No bin/jom.dll in ${installPath}`);
  const databasePath = path.join(installPath, 'system');
  const iniCandidates = [path.join(databasePath, 'jade.ini'), path.join(installPath, 'jade.ini')];
  const iniPath = iniCandidates.find(file => fs.existsSync(file));
  const ini = iniPath ? readIni(iniPath) : {};
  const configuredServer = ini.jade?.server?.toLowerCase();
  return { installPath, databasePath: fs.existsSync(path.join(databasePath, '_control.dat')) ? databasePath : '',
    iniPath: iniPath || '', serverFromIni: configuredServer === 'multiuser' ? 'multiUser' : configuredServer === 'singleuser' ? 'singleUser' : '',
    serverConnection: ini.jadeclient?.servernodespecifications || '' };
}

function resolveConfig(config, defaults = {}) {
  const resolved = { ...defaults, ...Object.fromEntries(Object.entries(config).filter(([, value]) => value !== undefined && value !== '')) };
  resolved.installPath ||= process.env.JADE_INSTALL || '';
  resolved.databasePath ||= resolved.installPath ? path.join(resolved.installPath, 'system') : '';
  resolved.iniPath ||= resolved.databasePath ? path.join(resolved.databasePath, 'jade.ini') : '';
  resolved.owner ||= 'Object';
  resolved.application ||= 'RootSchemaApp';
  resolved.server ||= 'singleUser';
  resolved.run = Boolean(resolved.run);
  for (const key of ['installPath', 'databasePath', 'iniPath', 'program', 'schema']) {
    if (!resolved[key]) throw new Error(`Set ${key} in launch.json or the Jade Native Runner settings.`);
  }
  if (!['singleUser', 'multiUser'].includes(resolved.server)) throw new Error('server must be singleUser or multiUser.');
  if (resolved.expectedInteger !== undefined && !resolved.run) throw new Error('expectedInteger requires run=true.');
  if (resolved.run && resolved.owner !== 'Object') throw new Error('Run currently supports Object-owned Integer methods; use run=false for other classes.');
  if (/\.(cls|scm)$/i.test(resolved.program) && !resolved.classFile) throw new Error('This checker expects a method file, not a CLS/SCM schema extract.');
  if (resolved.classFile && resolved.run) throw new Error('CLS checks compile methods only; publishing is a separate action.');
  if (resolved.offline && (!resolved.classFile || resolved.run)) throw new Error('Offline checks require a CLS file and run=false.');
  for (const [label, file] of [['Jade native library', path.join(resolved.installPath, 'bin', 'jom.dll')],
    ['database control file', path.join(resolved.databasePath, '_control.dat')], ['INI file', resolved.iniPath], ['method source', resolved.program]]) {
    if (resolved.offline && ['database control file', 'INI file'].includes(label)) continue;
    if (!fs.existsSync(file)) throw new Error(`${label} not found: ${file}`);
  }
  return resolved;
}

module.exports = { parseIni, readIni, schemaFromSource, hasMethodBodies, inspectInstall, resolveConfig };
