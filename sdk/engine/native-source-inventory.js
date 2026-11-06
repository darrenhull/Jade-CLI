"use strict";
const path = require("node:path");
const { snapshotProject } = require("./incremental-project"),
  { hash } = require("./native-project");
const canonical = (file) => path.resolve(file).toLowerCase();
function snapshotInputs(project, snapshot = snapshotProject(project)) {
  const inputs = {};
  for (const schema of project.schemas) {
    const resources = [
        schema.header,
        ...schema.entries.filter((entry) => entry.extension !== ".cls"),
      ],
      resourceSet = hash(
        Buffer.from(
          JSON.stringify(
            [
              ...new Set(resources.map((entry) => canonical(entry.file))),
            ].sort(),
          ),
        ),
      );
    for (const entry of resources)
      inputs[canonical(entry.file)] = {
        kind: "resource",
        hash: entry.hash,
        group: schema.name,
        resourceSet,
      };
    for (const entry of schema.classes)
      inputs[canonical(entry.file)] = {
        kind: "class",
        hash: entry.hash,
        shape: snapshot.classes?.[schema.name + "::" + entry.owner]?.shape,
        schema: schema.name,
        owner: entry.owner,
      };
  }
  for (const entry of project.migrations || [])
    inputs[canonical(entry.file)] = {
      kind: "resource",
      hash: entry.hash,
      group: "migrations",
      resourceSet: hash(
        Buffer.from(
          JSON.stringify(
            project.migrations.map((entry) => canonical(entry.file)).sort(),
          ),
        ),
      ),
    };
  return inputs;
}
function inventoryPlan(
  project,
  inventory = {},
  inputs = snapshotInputs(project),
) {
  const entries = Object.entries(inputs),
    known = (value) => typeof value === "string" && value.length > 0;
  const same = (before, after) =>
    before?.kind === after.kind &&
    known(after.hash) &&
    before.hash === after.hash &&
    (after.kind !== "resource" ||
      (before.resourceSet === after.resourceSet &&
        before.group === after.group));
  const covered =
    entries.length > 0 &&
    entries.every(([key, value]) => same(inventory[key], value));
  if (covered) return { covered: true, inputs };
  // Resources must be exactly known; only methods inside known declarations can
  // take the native partial CLS path. No result, method selection or run status is cached.
  if (
    entries.some(
      ([key, value]) =>
        value.kind === "resource" && !same(inventory[key], value),
    )
  )
    return { covered: false, inputs };
  const changed = [];
  for (const schema of project.schemas)
    for (const entry of schema.classes) {
      const key = canonical(entry.file),
        now = inputs[key],
        before = inventory[key];
      if (
        before?.kind !== "class" ||
        !known(now.shape) ||
        before.shape !== now.shape ||
        before.schema !== now.schema ||
        before.owner !== now.owner
      )
        return { covered: false, inputs };
      if (before.hash !== now.hash) changed.push(entry);
    }
  return {
    covered: false,
    inputs,
    ...(changed.length ? { delta: changed } : {}),
  };
}
module.exports = { snapshotInputs, inventoryPlan, canonical };
