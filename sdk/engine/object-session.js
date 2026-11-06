"use strict";
const fs = require("node:fs"),
  path = require("node:path"),
  { EventEmitter } = require("node:events");
const { workingLocation } = require("./working-database");
const commands = new Set([
  "catalog",
  "instances",
  "object",
  "collection",
  "release",
]);
function inspectionTarget(config, selection, dependencies = {}) {
  if (!["working", "source"].includes(selection))
    throw Error("Invalid database selection.");
  const base = { ...config, readOnly: true, selection };
  if (selection === "source") return base;
  const location = workingLocation(config),
    marker = path.join(location.root, "working-database.json"),
    exists = dependencies.exists || fs.existsSync;
  if (
    !exists(marker) ||
    !exists(path.join(location.databasePath, "_control.dat"))
  )
    throw Error(
      "Compile or run tests to create the working database before inspecting it.",
    );
  const saved = (
    dependencies.readJson ||
    ((file) => JSON.parse(fs.readFileSync(file, "utf8")))
  )(marker);
  if (saved.identity !== location.identity)
    throw Error("Working database belongs to a different source connection.");
  return {
    ...base,
    root: location.root,
    databasePath: location.databasePath,
    iniPath: path.join(location.root, "working.ini"),
    server: "singleUser",
  };
}
class InspectionSession extends EventEmitter {
  constructor(options) {
    super();
    this.options = options;
    this.target = "working";
    this.generation = 0;
    this.history = [];
    this.position = -1;
    this.client = undefined;
  }
  async connect() {
    if (this.disconnecting) await this.disconnecting;
    if (this.client) {
      await this.client.ready;
      return this.client;
    }
    const generation = this.generation,
      client = await this.options.connect(this.target);
    if (generation !== this.generation) {
      await client.stop();
      throw Error("Database selection changed while connecting.");
    }
    this.client = client;
    client.once("close", () => {
      if (this.client === client) {
        this.client = undefined;
        this.generation++;
        this.resetHistory();
        this.emit("change");
      }
    });
    try {
      await client.ready;
      if (generation !== this.generation)
        throw Error("Inspection connection was cancelled.");
      this.emit("change");
      return client;
    } catch (error) {
      await client.stop();
      if (this.client === client) this.client = undefined;
      throw error;
    }
  }
  async request(command, args = {}) {
    if (!commands.has(command))
      throw Error(
        "Object Explorer supports read-only inspection operations only.",
      );
    const client = this.client,
      generation = this.generation;
    if (!client) throw Error("Connect to a database first.");
    const result = await client.request(command, args);
    if (generation !== this.generation || client !== this.client)
      throw Error("Database changed or inspection was cancelled.");
    return result;
  }
  resetHistory() {
    this.history = [];
    this.position = -1;
  }
  visit(entry) {
    if (this.position >= 0 && this.history[this.position]?.oid === entry.oid)
      return;
    this.history.splice(this.position + 1);
    this.history.push({ ...entry });
    if (this.history.length > 100) this.history.shift();
    this.position = this.history.length - 1;
    this.emit("history");
  }
  back() {
    if (this.position > 0) this.position--;
    return this.history[this.position];
  }
  forward() {
    if (this.position < this.history.length - 1) this.position++;
    return this.history[this.position];
  }
  async disconnect() {
    if (this.disconnecting) return this.disconnecting;
    this.generation++;
    const client = this.client;
    this.client = undefined;
    this.resetHistory();
    this.emit("change");
    if (client) {
      const closing = Promise.resolve(client.stop());
      this.disconnecting = closing;
      try {
        await closing;
      } finally {
        if (this.disconnecting === closing) this.disconnecting = undefined;
      }
    }
  }
  async select(target) {
    if (!["working", "source"].includes(target))
      throw Error("Invalid database selection.");
    await this.disconnect();
    this.target = target;
    this.emit("change");
  }
}
module.exports = { InspectionSession, inspectionTarget };
