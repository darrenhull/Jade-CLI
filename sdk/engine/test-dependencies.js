"use strict";
const fs = require("node:fs"),
  path = require("node:path");
const { parseSource } = require("./source_symbols");
// Select source inputs only. Jade still resolves and validates all types and code.
async function testDependencies(
  files,
  program,
  resources = [],
  { project } = {},
) {
  // Reuse only this invocation's freshly scanned bytes/models. Nothing survives
  // into another run, so disk edits cannot be hidden by watcher/mtime caching.
  const key = (file) => path.resolve(file).toLowerCase(),
    snapshots = new Map(),
    parsed = new Map();
  for (const entry of project?.entries || [])
    if (typeof entry.text === "string")
      snapshots.set(key(entry.file), entry.text);
  for (const resource of resources)
    if (typeof resource.text === "string" && !snapshots.has(key(resource.file)))
      snapshots.set(key(resource.file), resource.text);
  for (const schema of project?.schemas || [])
    for (const entry of schema.classes || [])
      if (entry.model)
        parsed.set(key(entry.file), Promise.resolve(entry.model));
  function modelFor(file) {
    const id = key(file);
    if (!parsed.has(id))
      parsed.set(
        id,
        (async () =>
          parseSource(
            snapshots.has(id)
              ? snapshots.get(id)
              : await fs.promises.readFile(file, "utf8"),
            file,
          ))(),
      );
    return parsed.get(id);
  }
  const models = new Map(),
    classes = new Map(),
    parents = new Map();
  let next = 0;
  await Promise.all(
    Array.from({ length: Math.min(8, files.length) }, async () => {
      while (next < files.length) {
        const file = files[next++],
          model = await modelFor(file);
        const entry = {
          file,
          schema: model.schema,
          parentSchema: model.parentSchema,
          classes: model.classes.map(({ name, parent, partial }) => ({
            name,
            parent,
            partial,
          })),
        };
        models.set(path.resolve(file).toLowerCase(), entry);
        if (model.schema) {
          const prior = parents.get(model.schema);
          if (prior && prior !== model.parentSchema)
            throw new Error("Conflicting superschemas for " + model.schema);
          parents.set(model.schema, model.parentSchema);
        }
        for (const cls of entry.classes) {
          const id = entry.schema + "::" + cls.name;
          if (!classes.has(id)) classes.set(id, []);
          classes.get(id).push({ entry, cls });
        }
      }
    }),
  );
  for (const resource of resources)
    if (resource.parentSchema)
      parents.set(resource.schema, resource.parentSchema);
  const selected = models.get(path.resolve(program).toLowerCase());
  if (!selected)
    throw new Error("Selected test source is not in the workspace.");
  const needed = new Set(),
    queue = [selected];
  function namespaces(schema) {
    const result = [],
      seen = new Set();
    while (schema && !seen.has(schema)) {
      seen.add(schema);
      result.push(schema);
      schema = parents.get(schema);
    }
    return result;
  }
  function include(name, schema) {
    for (const namespace of namespaces(schema)) {
      const entries = classes.get(namespace + "::" + name) || [];
      if (entries.filter((row) => !row.cls.partial).length > 1)
        throw new Error(
          "Multiple complete class declarations: " + namespace + "::" + name,
        );
      for (const row of entries)
        if (!needed.has(row.entry.file)) queue.push(row.entry);
      if (entries.some((row) => !row.cls.partial)) break;
    }
  }
  // Runtime application/global/session overrides are implicit dependencies.
  for (const namespace of namespaces(selected.schema))
    for (const entry of models.values())
      if (
        entry.schema === namespace &&
        entry.classes.some((cls) =>
          [
            "RootSchemaApp",
            "Application",
            "RootSchemaGlobal",
            "RootSchemaSession",
          ].includes(cls.parent),
        )
      )
        queue.push(entry);
  for (const resource of resources)
    if (namespaces(selected.schema).includes(resource.schema))
      queue.push({ ...resource, resource: true });
  for (let cursor = 0; cursor < queue.length; cursor++) {
    const entry = queue[cursor];
    if (needed.has(entry.file)) continue;
    needed.add(entry.file);
    const model = await modelFor(entry.file);
    const code =
      entry.resource && entry.extension === ".ddx"
        ? (snapshots.get(key(entry.file)) ?? entry.text ?? model.text)
        : model.code;
    for (const name of new Set(code.match(/\b[A-Za-z_]\w*\b/g) || []))
      include(name, entry.schema);
  }
  return files.filter((file) => needed.has(file));
}
module.exports = { testDependencies };
