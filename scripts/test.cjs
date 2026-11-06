'use strict';
const fs = require('node:fs');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const root = path.resolve(__dirname, '..');
const files = [];
function collect(folder) {
  for (const entry of fs.readdirSync(folder, { withFileTypes: true })) {
    const file = path.join(folder, entry.name);
    if (entry.isDirectory()) collect(file);
    else if (entry.name.endsWith('_tests.test.js')) files.push(file);
  }
}
collect(path.join(root, 'tests'));
if (!files.length) throw new Error('No grouped CLI tests found.');
const env = { ...process.env };
for (const key of Object.keys(env)) {
  if (key.startsWith('JADE_') && key !== 'JADE_CLI_STANDALONE_EXE') delete env[key];
}
const result = spawnSync(process.execPath, ['--test', ...files.sort()], {
  cwd: root,
  env,
  stdio: 'inherit'
});
process.exitCode = result.status ?? 1;
