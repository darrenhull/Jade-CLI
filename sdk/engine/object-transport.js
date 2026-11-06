"use strict";
const { spawn } = require("node:child_process"),
  { EventEmitter } = require("node:events"),
  { StringDecoder } = require("node:string_decoder");
class InspectorTransport extends EventEmitter {
  constructor(executable, session, options = {}) {
    super();
    this.options = {
      readyTimeout: 20000,
      requestTimeout: 20000,
      stopTimeout: 3000,
      maxBytes: 8 * 1024 * 1024,
      ...options,
    };
    this.pending = new Map();
    this.sequence = 0;
    this.state = "starting";
    this.buffer = "";
    this.decoder = new StringDecoder("utf8");
    this.errors = new StringDecoder("utf8");
    this.ready = new Promise((resolve, reject) => {
      this.readyResolve = resolve;
      this.readyReject = reject;
    });
    this.closed = new Promise((resolve) => (this.closedResolve = resolve));
    this.child = (options.spawn || spawn)(executable, [session], {
      windowsHide: true,
      stdio: ["pipe", "pipe", "pipe"],
    });
    this.child.on("error", (error) => this.fail(error));
    this.child.stdin.on("error", (error) => this.fail(error));
    this.child.stdout.on("data", (bytes) => this.read(bytes));
    this.child.stderr.on("data", (bytes) =>
      this.emit("output", this.errors.write(bytes)),
    );
    this.exited = new Promise(resolve => this.child.once("close", code => { this.close(code); resolve(code); }));
    this.startTimer = setTimeout(
      () =>
        this.fail(
          Error("Jade inspection did not connect within its time limit."),
        ),
      this.options.readyTimeout,
    );
  }
  read(bytes) {
    if (this.state === "closed") return;
    this.buffer += this.decoder.write(bytes);
    let newline;
    while ((newline = this.buffer.indexOf("\n")) >= 0) {
      const line = this.buffer.slice(0, newline);
      this.buffer = this.buffer.slice(newline + 1);
      if (Buffer.byteLength(line) > this.options.maxBytes) {
        this.fail(Error("Inspection response exceeded its size limit."));
        return;
      }
      let packet;
      try {
        packet = JSON.parse(line);
      } catch {
        this.emit("output", line + "\n");
        continue;
      }
      if (packet.protocol !== (this.options.protocol || "jade-inspect/1")) {
        this.emit("output", line + "\n");
        continue;
      }
      if (Number.isInteger(packet.id)) {
        const p = this.pending.get(packet.id);
        if (!p) continue;
        this.pending.delete(packet.id);
        clearTimeout(p.timer);
        if (packet.success) p.resolve(packet.body || {});
        else {
          const error = Error(packet.message || "Native inspection failed.");
          error.code = packet.code;
          p.reject(error);
        }
      } else if (packet.event === "ready") {
        if (this.options.validateReady ? !this.options.validateReady(packet) : packet.readOnly !== true && packet.body?.readOnly !== true) {
          this.fail(
            Error("Native inspector did not confirm a read-only connection."),
          );
          return;
        }
        this.state = "ready";
        clearTimeout(this.startTimer);
        this.readyResolve(packet);
      } else if (packet.event === "error") {
        const error = Error(packet.message || "Jade inspection failed.");
        error.code = packet.code;
        this.fail(error);
        return;
      } else this.emit("message", packet);
    }
    if (Buffer.byteLength(this.buffer) > this.options.maxBytes)
      this.fail(Error("Inspection response exceeded its size limit."));
  }
  request(command, args = {}) {
    if (
      this.state === "closed" ||
      (this.state === "stopping" && command !== "close")
    )
      return Promise.reject(Error("Inspection connection is closed."));
    return new Promise((resolve, reject) => {
      const id = ++this.sequence,
        timer = setTimeout(() => {
          this.pending.delete(id);
          reject(Error("Jade inspection request timed out: " + command));
          this.stop();
        }, this.options.requestTimeout);
      this.pending.set(id, { resolve, reject, timer });
      this.child.stdin.write(
        JSON.stringify({ ...args, id, command }) + "\n",
        (error) => {
          if (!error) return;
          const p = this.pending.get(id);
          if (p) {
            this.pending.delete(id);
            clearTimeout(p.timer);
            p.reject(error);
          }
        },
      );
    });
  }
  fail(error) {
    if (this.state === "closed") return;
    this.readyReject(error);
    this.emit("failure", error);
    this.child.kill();
    this.close(2);
  }
  stop() {
    if (this.state === "closed" || this.state === "stopping")
      return this.closed;
    this.state = "stopping";
    clearTimeout(this.startTimer);
    this.readyReject(Error("Inspection connection was cancelled."));
    void this.request("close").catch(() => {});
    this.stopTimer = setTimeout(() => {
      if (this.state !== "closed") this.child.kill();
    }, this.options.stopTimeout);
    return this.closed;
  }
  close(code) {
    if (this.state === "closed") return;
    this.state = "closed";
    clearTimeout(this.startTimer);
    clearTimeout(this.stopTimer);
    const error = Error("Inspection connection closed (" + code + ").");
    this.readyReject(error);
    for (const p of this.pending.values()) {
      clearTimeout(p.timer);
      p.reject(error);
    }
    this.pending.clear();
    this.emit("close", code);
    this.closedResolve(code);
  }
}
module.exports = { InspectorTransport };
