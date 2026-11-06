import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const readJson = file => JSON.parse(fs.readFileSync(path.join(root, file), 'utf8'));
const manifest = readJson('package.json');
if (manifest.name !== 'jade-tools-cli' || manifest.version !== '0.1.0' || manifest.private !== true)
  throw new Error('Unexpected CLI package identity/version/private flag.');
if (manifest.bin?.['jade-cli'] !== 'bin/jade-cli.js')
  throw new Error('CLI executable mapping is missing.');
for (const file of ['bin/jade-cli.js', 'sdk/index.js', 'sdk/broker.js', 'sdk/broker-client.js', 'sdk/native/jade-test-helper.exe', 'sdk/native/jade-inspect-helper.exe', 'sdk/native/jade-debug-helper.exe', 'sdk/native/jade-native-helper.exe', 'sdk/vendor/sax.LICENSE.md'])
  if (!fs.existsSync(path.join(root, file))) throw new Error(`Missing packaged runtime asset: ${file}`);
const workspace = readJson('Jade-CLI.code-workspace');
if (workspace.folders?.length !== 1 || workspace.folders[0].path !== '.')
  throw new Error('Workspace must open only this independent repository.');
const docs = ['README.md'];
for (const file of docs) {
  const text = fs.readFileSync(path.join(root, file), 'utf8');
  if (!text.trim()) throw new Error(`Empty document: ${file}`);
  for (const [, target] of text.matchAll(/\]\(([^)]+)\)/g)) {
    if (/^(?:https?:|#)/i.test(target)) continue;
    const resolved = path.resolve(root, path.dirname(file), decodeURIComponent(target.split('#')[0]));
    const relative = path.relative(root, resolved);
    if (relative.startsWith('..') || path.isAbsolute(relative) || !fs.existsSync(resolved))
      throw new Error(`Broken or external local link in ${file}: ${target}`);
  }
}
console.log('CLI manifests, required runtime assets and README links are valid. Run native controls separately.');
