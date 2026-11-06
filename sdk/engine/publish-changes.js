'use strict';
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const crypto = require('node:crypto');
const { resolvePublishConfig, startPublish } = require('./publish');
const { isNestedSnapshot } = require('./source-snapshots');

const digest = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const canonical = file => path.resolve(file).toLowerCase();
const targetKey = config => digest(JSON.stringify([canonical(config.databasePath), canonical(config.iniPath || path.join(config.databasePath, 'jade.ini')), config.server || 'singleUser']));
const fileKey = config => canonical(config.source) + '::' + config.schema;
const contains = (root, file) => { const relative = path.relative(root, file); return relative !== '..' && !relative.startsWith('..' + path.sep) && !path.isAbsolute(relative); };

async function planChangedFiles(files, root, settings, published = {}) {
  const manifests = new Map();
  async function pulledBaseline(file, config) {
    let directory = path.dirname(file);
    while (contains(root, directory)) {
      const manifestPath = path.join(directory, 'extract-manifest.json');
      if (!manifests.has(manifestPath)) manifests.set(manifestPath, (async () => {
        try {
          const manifest = JSON.parse(await fs.promises.readFile(manifestPath, 'utf8'));
          if (!manifest.database || canonical(manifest.database) !== canonical(config.databasePath)) return undefined;
          return { date: manifest.extractedUtc, files: new Map(manifest.files.map(row => [row.path, row.sha256])) };
        } catch (error) { if (error.code === 'ENOENT') return undefined; throw error; }
      })());
      const manifest = await manifests.get(manifestPath);
      const hash = manifest?.files.get(path.relative(directory, file).split(path.sep).join('/'));
      if (hash) return { hash, date: manifest.date };
      if (directory === path.resolve(root)) break;
      directory = path.dirname(directory);
    }
  }
  let next = 0;
  const entries = [], declarations = new Map();
  // Bounded asynchronous reads keep large source trees responsive.
  await Promise.all(Array.from({ length: Math.min(8, files.length) }, async () => {
    while (next < files.length) {
      const source = files[next++];
      const bytes = await fs.promises.readFile(source), hash = digest(bytes);
      const config = resolvePublishConfig({ ...settings, program: source, sourceText: bytes.toString('utf8') });
      const text = bytes.toString('utf8');
      const header = /^\s*(\w+)\s+subclassOf\s+(\w+)/m.exec(text.split('typeDefinitions', 1)[0]);
      if (header) {
        const key = config.schema + '::' + header[1];
        if (declarations.has(key)) throw new Error('Multiple complete class declarations: ' + key);
        declarations.set(key, source);
      }
      const saved = published[targetKey(config)]?.[fileKey(config)];
      const pulled = await pulledBaseline(source, config);
      const baseline = pulled && (!saved || Date.parse(pulled.date) > Date.parse(saved.date)) ? pulled : saved || pulled;
      if (hash === baseline?.hash) continue;
      const parentSchema = /^\s*\w+\s+subschemaOf\s+(\w+)/m.exec(text)?.[1];
      entries.push({ source, hash, config, schema: config.schema, parentSchema, owner: header?.[1], parent: header?.[2],
        reason: baseline ? 'modified' : 'new / no baseline' });
    }
  }));
  // Load superschemas and changed base classes before their children.
  const ordered = [], visiting = new Set(), visited = new Set();
  const sorted = entries.sort((a, b) => a.source.localeCompare(b.source));
  const schemas = new Map(sorted.map(row => [row.schema, row.parentSchema]));
  const schemaFiles = new Map(), doneSchemas = new Set();
  for (const row of sorted) { if (!schemaFiles.has(row.schema)) schemaFiles.set(row.schema, []); schemaFiles.get(row.schema).push(row); }
  const classes = new Map();
  for (const row of sorted) if (row.owner) {
    const key = row.schema + '::' + row.owner;
    if (classes.has(key)) throw new Error('Multiple complete class declarations: ' + key);
    classes.set(key, row);
  }
  function visit(row) {
    if (visited.has(row)) return;
    if (visiting.has(row)) throw new Error('Cyclic publish dependency: ' + row.schema + '::' + row.owner);
    visiting.add(row);
    if (!doneSchemas.has(row.parentSchema)) {
      for (const candidate of schemaFiles.get(row.parentSchema) || []) visit(candidate);
      doneSchemas.add(row.parentSchema);
    }
    let namespace = row.schema;
    const seen = new Set();
    while (namespace && !seen.has(namespace)) {
      seen.add(namespace);
      const base = classes.get(namespace + '::' + row.parent);
      if (base) { visit(base); break; }
      namespace = schemas.get(namespace);
    }
    visiting.delete(row); visited.add(row); ordered.push(row);
  }
  sorted.forEach(visit);
  return ordered;
}

async function publishPlan(plan, publish, accepted, output = () => {}) {
  const temporary = await fs.promises.mkdtemp(path.join(os.tmpdir(), 'jade-publish-changes-'));
  const results = [];
  try {
    // Freeze the entire reviewed batch before the first database update.
    for (const [index, entry] of plan.entries()) {
      const bytes = await fs.promises.readFile(entry.source);
      if (digest(bytes) !== entry.hash) throw new Error('Source changed after review. Run publish again: ' + entry.source);
      const directory = path.join(temporary, String(index));
      await fs.promises.mkdir(directory);
      entry.staged = path.join(directory, path.basename(entry.source));
      await fs.promises.writeFile(entry.staged, bytes);
    }
    for (const entry of plan) {
      output(`Publishing ${entry.source} to ${entry.schema} (${entry.config.databasePath}) ...\n`);
      const config = { ...entry.config, source: entry.staged,
        args: entry.config.args.map(argument => argument.startsWith('schemaFile=') ? 'schemaFile=' + entry.staged : argument) };
      const result = await publish(config, output);
      results.push({ ...result, source: entry.source, schema: entry.schema });
      output(`LoaderExitCode=${result.code}\n`);
      if (!result.success) return { success: false, results, failed: entry.source, publishedWithErrors: result.publishedWithErrors };
      await accepted(entry);
    }
    return { success: true, results };
  } finally {
    if (path.dirname(path.resolve(temporary)) === path.resolve(os.tmpdir()) && path.basename(temporary).startsWith('jade-publish-changes-')) {
      await fs.promises.rm(temporary, { recursive: true, force: true });
    }
  }
}

function registerPublishChanges(vscode, context, options) {
  const stateKey = 'jadeNative.publishedFiles';
  async function recordPublished(config, hash) {
    const state = context.workspaceState?.get(stateKey, {}) || {};
    const target = targetKey(config);
    state[target] ||= {};
    state[target][fileKey(config)] = { hash, date: new Date().toISOString() };
    await context.workspaceState?.update(stateKey, state);
  }
  context.subscriptions.push(vscode.commands.registerCommand('jadeNative.publishChangedFiles', async () => {
    let locked = false;
    const output = options.output;
    try {
      if(options.publishNativeProject){const result=await options.publishNativeProject();if(result)return result;}
      await options.beforeOperation?.();
      if (options.isBusy()) throw new Error('A Jade operation is already running.');
      const folders = vscode.workspace.workspaceFolders || [];
      const active = vscode.window.activeTextEditor?.document.uri;
      let folder = active && vscode.workspace.getWorkspaceFolder(active);
      folder ||= folders.length === 1 ? folders[0] : (await vscode.window.showQuickPick(folders.map(item => ({ label: item.name, folder: item })), { title: 'Choose workspace to publish' }))?.folder;
      if (!folder) throw new Error('Open a workspace folder to publish its changed classes.');
      await options.setBusy(true); locked = true;
      const root = folder.uri.fsPath;
      for (const document of vscode.workspace.textDocuments) {
        if (document.isDirty && document.uri.scheme === 'file' && /\.cls$/i.test(document.uri.fsPath) && contains(root, document.uri.fsPath) && !isNestedSnapshot(document.uri, vscode.workspace)) {
          if (!await document.save()) throw new Error('Could not save: ' + document.uri.fsPath);
        }
      }
      output.clear(); output.show(true); output.appendLine('Checking workspace CLS files for changes ...');
      const files = (await vscode.workspace.findFiles('**/*.cls', '**/{node_modules,.git}/**'))
        .filter(uri => contains(root, uri.fsPath) && !isNestedSnapshot(uri, vscode.workspace)).map(uri => uri.fsPath);
      const settings = options.settings(folder);
      const plan = await planChangedFiles(files, root, settings, context.workspaceState?.get(stateKey, {}) || {});
      if (!plan.length) { output.appendLine('No changed CLS files to publish.'); void vscode.window.showInformationMessage('No changed Jade class files to publish.'); return { success: true, results: [] }; }
      output.appendLine(plan.map(entry => `${entry.reason}: ${entry.schema} - ${path.relative(root, entry.source)}`).join('\n'));
      const detail = `Database: ${plan[0].config.databasePath}\n\n` + plan.slice(0, 12).map(entry => `${entry.schema}: ${path.relative(root, entry.source)} (${entry.reason})`).join('\n') +
        (plan.length > 12 ? `\n...and ${plan.length - 12} more. The full ordered list is in Jade Publish output; choose Review File List to read it before publishing.` : '') +
        '\n\nLoads the listed CLS files in order. Stops on the first loader failure; earlier successful loads remain committed. Files without a pull/publish baseline are included as new. Other native schema/form/section formats are not published by this class command.';
      const choice = await vscode.window.showWarningMessage(`Publish ${plan.length} changed Jade class file(s)?`, { modal: true, detail }, 'Update Jade Base', ...(plan.length > 12 ? ['Review File List'] : []));
      if (choice === 'Review File List') {
        output.show(true); output.appendLine('Review the file list above. Click Publish All Changed Files again when ready.');
        return { success: false, reviewed: true };
      }
      if (choice !== 'Update Jade Base') {
        output.appendLine('Publish cancelled. The Jade database was not updated.'); return { success: false, cancelled: true };
      }
      const result = await vscode.window.withProgress({ location: vscode.ProgressLocation.Notification, title: 'Publishing changed Jade classes', cancellable: false }, () =>
        publishPlan(plan, (config, write) => startPublish(config, write).completion,
          entry => recordPublished(entry.config, entry.hash), text => output.append(text)));
      output.appendLine(`Published ${result.results.filter(row => row.success).length}/${plan.length} changed class files.`);
      if (result.success) void vscode.window.showInformationMessage(`Published ${plan.length} changed Jade class file(s).`);
      else void vscode.window.showErrorMessage(`Publish stopped at ${path.basename(result.failed)}${result.publishedWithErrors ? ' (loaded with compiler errors)' : ''}. Earlier successful files remain published. See Jade Publish output.`);
      return result;
    } catch (error) {
      output.appendLine('Publish failed: ' + error.message); output.show(true); void vscode.window.showErrorMessage(error.message);
      return { success: false, error: error.message };
    } finally { if (locked) await options.setBusy(false); }
  }));
  return { recordPublished };
}

module.exports = { planChangedFiles, publishPlan, registerPublishChanges, digest, targetKey, fileKey };
