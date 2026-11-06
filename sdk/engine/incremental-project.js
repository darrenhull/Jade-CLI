"use strict";
const { hash } = require("./native-project");
function shape(entry) {
  const marker = entry.model.code.search(/^[\t ]*typeSources[\t ]*$/m);
  if (marker < 0) return hash(Buffer.from(entry.text));
  const methods = entry.model.classes.flatMap((cls) =>
    cls.members
      .filter((m) => m.kind === "method")
      .map((m) => ({
        owner: cls.name,
        name: m.name,
        parameters: m.parameters,
        returnType: m.returnType,
        flags: m.flags,
      })),
  );
  return hash(
    Buffer.from(entry.text.slice(0, marker) + JSON.stringify(methods)),
  );
}
function snapshotProject(project) {
  const resources = project.schemas.map((schema) => ({
    name: schema.name,
    parent: schema.parent,
    header: [schema.header.file, schema.header.hash],
    entries: schema.entries
      .filter((e) => e.extension !== ".cls")
      .map((e) => [e.file, e.hash])
      .sort(),
  }));
  const migrations = (project.migrations || [])
    .map((e) => [e.file, e.hash])
    .sort();
  const classes = {};
  for (const schema of project.schemas)
    for (const entry of schema.classes)
      classes[schema.name + "::" + entry.owner] = {
        file: entry.file,
        hash: entry.hash,
        shape: shape(entry),
      };
  return {
    resources: hash(Buffer.from(JSON.stringify([resources, migrations]))),
    classes,
  };
}
// Native partial loads are safe here because every declaration and method
// signature is identical to the last successful matching build scope. Anything structural goes
// through JadeJoinSchema and Jade's complete dependency/reorganisation handling.
function incrementalPlan(project, state, scope = "full") {
  const before =
    scope === "full"
      ? state.fullSnapshot
      : state.scopeSnapshot?.scope === scope
        ? state.scopeSnapshot.snapshot
        : undefined;
  if (!state.scopes?.[scope] || !before) return;
  const current = snapshotProject(project);
  if (
    before.resources !== current.resources ||
    Object.keys(before.classes).length !== Object.keys(current.classes).length
  )
    return;
  const changed = [];
  for (const schema of project.schemas)
    for (const entry of schema.classes) {
      const id = schema.name + "::" + entry.owner,
        old = before.classes[id],
        now = current.classes[id];
      if (!old || old.file !== now.file || old.shape !== now.shape) return;
      if (old.hash !== now.hash) changed.push(entry);
    }
  return changed.length ? changed : undefined;
}
module.exports = { snapshotProject, incrementalPlan };
