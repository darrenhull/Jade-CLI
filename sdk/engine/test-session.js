"use strict";
const fs = require("node:fs"),
  path = require("node:path"),
  crypto = require("node:crypto"),
  os = require("node:os");
const { InspectorTransport } = require("./object-transport");
// Owns one native working runtime. Jade owns loading, tests and native reports.
// External utilities, inspection and debugging release it before opening the base.
class NativeTestSession {
  constructor(options) {
    this.options = options;
    this.client = undefined;
    this.key = undefined;
    this.output = undefined;
  }
  async release() {
    const client = this.client;
    this.client = undefined;
    this.key = undefined;
    if (client) {
      await client.stop();
      await client.exited;
    }
  }
  run(working, selection, output = () => {}) {
    return this.request(
      working,
      "run",
      {
        controlFile: selection.controlFile,
        testName: selection.testName || "WorkspaceTests",
        logFile: selection.logFile,
      },
      output,
    );
  }
  load(working, args, output = () => {}) {
    return this.request(
      working,
      "load",
      { args: args.filter((value) => !/^(?:path|ini|server)=/i.test(value)) },
      output,
    );
  }
  async request(working, command, args, output = () => {}) {
    if (this.running)
      throw Error("A native Jade working operation is already running.");
    this.running = true;
    this.output = output;
    let aborting;
    const abort = () => {
      aborting = this.release();
    };
    working.signal?.addEventListener("abort", abort, { once: true });
    try {
      if (working.signal?.aborted)
        throw Error("Native Jade operation cancelled.");
      const key = JSON.stringify([
        path.resolve(working.databasePath).toLowerCase(),
        path.resolve(working.iniPath).toLowerCase(),
        path.resolve(working.installPath).toLowerCase(),
      ]);
      if (this.client && (this.key !== key || this.client.state === "closed"))
        await this.release();
      if (!this.client) {
        const directory = path.join(working.root, "test-sessions");
        fs.mkdirSync(directory, { recursive: true });
        const sessionFile = path.join(directory, crypto.randomUUID() + ".json");
        fs.writeFileSync(
          sessionFile,
          JSON.stringify({
            root: working.root,
            installPath: working.installPath,
            databasePath: working.databasePath,
            iniPath: working.iniPath,
            sourceDatabasePath: working.sourceDatabasePath,
            schema: "RootSchema",
            server: "singleUser",
            assemblyRoot: path.join(
              os.tmpdir(),
              "jd-" +
                crypto
                  .createHash("sha256")
                  .update(path.resolve(working.root).toLowerCase())
                  .digest("hex")
                  .slice(0, 8),
            ),
          }),
        );
        let client;
        try {
          client = await (
            this.options.connect ||
            ((exe, file, options) => new InspectorTransport(exe, file, options))
          )(this.options.executable, sessionFile, {
            protocol: "jade-test/1",
            validateReady: (p) => p.workingDatabase === true,
            requestTimeout: working.timeoutMilliseconds || 120000,
            readyTimeout: working.timeoutMilliseconds || 30000,
          });
          this.client = client;
          this.key = key;
          client.on("output", (text) => this.output?.(text));
          client.on("failure", () => {});
          client.once("close", () => {
            if (this.client === client) this.key = undefined;
          });
          await client.ready;
        } catch (error) {
          if (client) {
            await client.stop();
            await client.exited;
          }
          this.client = undefined;
          this.key = undefined;
          throw error;
        } finally {
          try {
            fs.unlinkSync(sessionFile);
          } catch (error) {
            if (error.code !== "ENOENT")
              output(
                "Could not remove native test startup file: " +
                  error.message +
                  "\n",
              );
          }
        }
      }
      if (working.signal?.aborted)
        throw Error("Native Jade operation cancelled.");
      const result = await this.client.request(command, args);
      if (result.output) output(result.output);
      const elapsed = result.loadMilliseconds ?? result.runMilliseconds;
      if (elapsed !== undefined)
        output(
          "Native " +
            (command === "load" ? "compile" : "test run") +
            ": " +
            Math.round(elapsed) +
            " ms; working connection retained.\n",
        );
      return result;
    } catch (error) {
      await (aborting || this.release());
      if (working.signal?.aborted)
        throw Error("Native Jade operation cancelled.");
      throw error;
    } finally {
      working.signal?.removeEventListener("abort", abort);
      await aborting;
      this.output = undefined;
      this.running = false;
    }
  }
  dispose() {
    void this.release();
  }
}
module.exports = { NativeTestSession };
