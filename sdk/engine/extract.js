"use strict";
const fs = require("node:fs");
const path = require("node:path");
const crypto = require("node:crypto");
const { spawn } = require("node:child_process");
const { StringDecoder } = require("node:string_decoder");

function resolveExtractionDirectory(workspaceRoot, value = ".") {
  const root = path.resolve(workspaceRoot);
  // A leading slash denotes the workspace root in this setting, so /src
  // behaves like src rather than selecting the root of the Windows drive.
  const requested = (value.trim() || ".").replace(/^\/(?!\/)/, "");
  const destination = path.resolve(root, requested);
  const relative = path.relative(root, destination);
  if (
    relative === ".." ||
    relative.startsWith(".." + path.sep) ||
    path.isAbsolute(relative)
  ) {
    throw new Error("Choose an extraction folder within this workspace.");
  }
  if (fs.existsSync(destination) && !fs.statSync(destination).isDirectory())
    throw new Error("The extraction folder points to a file.");
  return destination;
}

function resolveExtractConfig(settings) {
  const installPath = settings.installPath || process.env.JADE_INSTALL;
  if (!installPath)
    throw new Error("Set the Jade installation using Detect Runtime Settings.");
  const databasePath =
    settings.databasePath || path.join(installPath, "system");
  const iniPath = settings.iniPath || path.join(databasePath, "jade.ini");
  const executable = path.join(installPath, "bin", "jadclient.exe");
  for (const file of [executable, databasePath, iniPath]) {
    if (!fs.existsSync(file)) throw new Error("Not found: " + file);
  }
  const server = settings.server || "singleUser";
  if (!["singleUser", "multiUser"].includes(server))
    throw new Error("Invalid Jade server mode.");
  return { executable, databasePath, iniPath, server };
}

function extractArguments(config, destination) {
  return [
    `path=${config.databasePath}`,
    `ini=${config.iniPath}`,
    `server=${config.server}`,
    "schema=JadeSchema",
    "app=JadeBatchExtract",
    "startAppParameters",
    "Every",
    path.join(destination, "schemas.mul"),
    "<sections,ddxFormat,utf8Encoding>",
  ];
}

async function snapshot(destination, config, isCancelled = () => false) {
  const files = [],
    paths = [];
  function check() {
    if (isCancelled()) throw Error("Extraction cancelled.");
  }
  async function visit(directory) {
    check();
    for (const entry of await fs.promises.readdir(directory, {
      withFileTypes: true,
    })) {
      const file = path.join(directory, entry.name);
      if (entry.isDirectory()) await visit(file);
      else if (
        entry.isFile() &&
        /\.(mul|scm|sec|cls|ddx|ddb|jcf)$/i.test(entry.name)
      ) {
        paths.push(file);
      }
    }
  }
  await visit(destination);
  let next = 0;
  await Promise.all(
    Array.from({ length: Math.min(8, paths.length) }, async () => {
      while (next < paths.length && !isCancelled()) {
        const file = paths[next++],
          bytes = await fs.promises.readFile(file);
        files.push({
          path: path.relative(destination, file).split(path.sep).join("/"),
          bytes: bytes.length,
          sha256: crypto.createHash("sha256").update(bytes).digest("hex"),
        });
      }
    }),
  );
  check();
  files.sort((a, b) => a.path.localeCompare(b.path));
  if (
    !files.some((file) => file.path.endsWith(".scm")) ||
    !fs.existsSync(path.join(destination, "schemas.mul"))
  ) {
    throw new Error(
      "Jade returned success but did not produce a schema extraction. See Jade Extract output.",
    );
  }
  const report = {
    format: 1,
    extractedUtc: new Date().toISOString(),
    database: config.databasePath,
    extractor: config.executable,
    mode: "Every; sections; ddxFormat; utf8Encoding",
    exitCode: 0,
    files,
  };
  await fs.promises.writeFile(
    path.join(destination, "extract-manifest.json"),
    JSON.stringify(report, null, 2) + "\n",
  );
  check();
  return {
    fileCount: files.length,
    classCount: files.filter((file) => file.path.endsWith(".cls")).length,
    schemas: files
      .filter((file) => /^[^/]+\.scm$/i.test(file.path))
      .map((file) => file.path.slice(0, -4)),
  };
}

function startExtract(
  config,
  destination,
  output = () => {},
  spawnProcess = spawn,
) {
  // mkdir is deliberately exclusive: never refresh over someone's edited files.
  fs.mkdirSync(destination);
  const log = fs.openSync(path.join(destination, "extract.log"), "wx");
  const decoder = new StringDecoder("utf8"),
    errors = new StringDecoder("utf8");
  let cancelled = false,
    child;
  function append(text) {
    if (text) {
      fs.writeSync(log, text);
      output(text);
    }
  }
  const completion = new Promise((resolve, reject) => {
    try {
      child = spawnProcess(
        config.executable,
        extractArguments(config, destination),
        { windowsHide: true, shell: false },
      );
    } catch (error) {
      fs.closeSync(log);
      reject(error);
      return;
    }
    child.stdout.on("data", (bytes) => append(decoder.write(bytes)));
    child.stderr.on("data", (bytes) => append(errors.write(bytes)));
    let launchError;
    child.once("error", (error) => {
      launchError = error;
    });
    child.once("close", async (code, signal) => {
      append(decoder.end());
      append(errors.end());
      fs.closeSync(log);
      if (launchError) {
        reject(launchError);
        return;
      }
      if (cancelled || code !== 0) {
        resolve({ success: false, cancelled, code, signal, destination });
        return;
      }
      try {
        resolve({
          success: true,
          code,
          destination,
          ...(await snapshot(destination, config, () => cancelled)),
        });
      } catch (error) {
        if (cancelled)
          resolve({
            success: false,
            cancelled: true,
            code,
            signal,
            destination,
          });
        else reject(error);
      }
    });
  });
  return {
    completion,
    stop() {
      cancelled = true;
      if (child && child.exitCode === null) child.kill();
    },
  };
}

function classIdentity(text) {
  const schema = /^\s*(\w+)\s+subschemaOf\b/m.exec(text)?.[1];
  const header = /^\s*(\w+)\s+subclassOf\b/m.exec(
    text.split("typeDefinitions")[0],
  )?.[1];
  if (
    !schema ||
    !header ||
    !new RegExp("^\\s*" + header + "\\s+completeDefinition\\b", "m").test(text)
  )
    return undefined;
  return schema + "::" + header;
}

function planSourceUpdate(staging, destination, previousManifest) {
  const manifest = JSON.parse(
    fs.readFileSync(path.join(staging, "extract-manifest.json"), "utf8"),
  );
  const organized = [
    "schema-groups-v1",
    "schema-sections-v2",
    "schema-sections-v3",
  ].includes(manifest.layout);
  const existingClasses = new Map();
  function index(directory) {
    if (!fs.existsSync(directory)) return;
    for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
      const file = path.join(directory, entry.name);
      if (
        entry.isDirectory() &&
        ![".git", "node_modules"].includes(entry.name) &&
        !fs.existsSync(path.join(file, ".jade-extraction"))
      )
        index(file);
      else if (entry.isFile() && /\.cls$/i.test(file)) {
        const key = classIdentity(
          fs.readFileSync(file, "utf8").replace(/^\uFEFF/, ""),
        );
        if (key) {
          if (!existingClasses.has(key)) existingClasses.set(key, []);
          existingClasses.get(key).push(file);
        }
      }
    }
  }
  index(destination);
  let previous = {};
  let previousFiles = [];
  const previousFile = path.join(destination, "extract-manifest.json");
  if (previousManifest || fs.existsSync(previousFile)) {
    const saved =
      previousManifest ||
      JSON.parse(fs.readFileSync(previousFile, "utf8").replace(/^\uFEFF/, ""));
    previousFiles = saved.files;
    previous = Object.fromEntries(
      saved.files.map((file) => [file.path, file.sha256]),
    );
  }
  const previousByNative = new Map(
    previousFiles.map((file) => [file.nativePath || file.path, file]),
  );
  const files = manifest.files.map((incoming) => {
    let file = { ...incoming, sourcePath: incoming.path };
    if (/\.cls$/i.test(file.path)) {
      const key = classIdentity(
        fs
          .readFileSync(path.join(staging, file.path), "utf8")
          .replace(/^\uFEFF/, ""),
      );
      const matches = existingClasses.get(key) || [];
      if (matches.length > 1)
        throw new Error(
          "Multiple existing complete declarations of " +
            key +
            ": " +
            matches.join(", "),
        );
      if (matches.length) {
        if (organized) file.originalPath = matches[0];
        else
          file.path = path
            .relative(destination, matches[0])
            .split(path.sep)
            .join("/");
      }
    }
    const target = path.resolve(destination, file.path);
    const relative = path.relative(path.resolve(destination), target);
    if (
      relative === ".." ||
      relative.startsWith(".." + path.sep) ||
      path.isAbsolute(relative)
    )
      throw new Error("Invalid extracted source path.");
    if (!file.originalPath && !file.generated && file.nativePath) {
      const old = previousByNative.get(file.nativePath);
      if (old) {
        const candidate = path.resolve(destination, old.path),
          oldRelative = path.relative(path.resolve(destination), candidate);
        if (
          oldRelative === ".." ||
          oldRelative.startsWith(".." + path.sep) ||
          path.isAbsolute(oldRelative)
        )
          throw new Error("Invalid previous source path.");
        if (fs.existsSync(candidate)) file.originalPath = candidate;
      }
    }
    if (file.originalPath === target) delete file.originalPath;
    if (file.originalPath && fs.existsSync(target))
      throw new Error("Both old and new source paths exist: " + file.path);
    const currentPath = file.originalPath || target;
    if (!fs.existsSync(currentPath)) return { ...file, target, action: "add" };
    if (!fs.statSync(currentPath).isFile())
      throw new Error("Expected a source file: " + currentPath);
    const current = crypto
      .createHash("sha256")
      .update(fs.readFileSync(currentPath))
      .digest("hex");
    const baseline =
      previous[file.path] ||
      previous[
        path.relative(destination, currentPath).split(path.sep).join("/")
      ];
    const action =
      current === file.sha256
        ? "unchanged"
        : current === baseline
          ? "update"
          : file.sha256 === baseline
            ? "keep"
            : "conflict";
    return { ...file, target, current, action };
  });
  const newPaths = new Set(files.map((file) => file.path));
  const movedPaths = new Set(
    files.map((file) => file.originalPath).filter(Boolean),
  );
  const obsolete = [],
    retainedLegacy = [];
  const explicitlyOmitted = new Set(
    (manifest.omittedNativeFiles || []).map((file) => file.nativePath),
  );
  if (organized)
    for (const old of previousFiles) {
      const target = path.resolve(destination, old.path),
        relative = path.relative(path.resolve(destination), target);
      if (
        relative === ".." ||
        relative.startsWith(".." + path.sep) ||
        path.isAbsolute(relative)
      )
        throw new Error("Invalid previous source path.");
      const omitted = explicitlyOmitted.has(old.nativePath || old.path);
      if (
        newPaths.has(old.path) ||
        movedPaths.has(target) ||
        (/\.cls$/i.test(old.path) && !omitted) ||
        !fs.existsSync(target)
      )
        continue;
      if (
        crypto
          .createHash("sha256")
          .update(fs.readFileSync(target))
          .digest("hex") === old.sha256
      )
        obsolete.push({ target, hash: old.sha256 });
      else if (
        manifest.layout === "schema-sections-v2" &&
        (omitted || /\.(scm|mul)$/i.test(old.path))
      )
        retainedLegacy.push({ path: old.path, target, reason: "modified" });
    }
  manifest.files = files.map(
    ({ target, current, action, sourcePath, originalPath, ...file }) => file,
  );
  return {
    staging,
    destination,
    manifest,
    files,
    obsolete,
    retainedLegacy,
    conflicts: files.filter((file) => file.action === "conflict"),
  };
}

function applySourceUpdate(plan, replaceConflicts = false, options = {}) {
  // Check again after the review dialog, before changing any source files.
  for (const file of plan.files) {
    const currentPath = file.originalPath || file.target;
    if (file.originalPath && fs.existsSync(file.target))
      throw new Error("Source appeared during extraction: " + file.path);
    if (!fs.existsSync(currentPath)) {
      if (file.action !== "add")
        throw new Error("Source changed during extraction: " + file.path);
    } else {
      const hash = crypto
        .createHash("sha256")
        .update(fs.readFileSync(currentPath))
        .digest("hex");
      if (file.action === "add" || hash !== file.current)
        throw new Error("Source changed during extraction: " + file.path);
    }
  }
  for (const old of plan.obsolete || []) {
    if (
      crypto
        .createHash("sha256")
        .update(fs.readFileSync(old.target))
        .digest("hex") !== old.hash
    )
      throw new Error("Source changed during extraction: " + old.target);
  }
  fs.mkdirSync(plan.destination, { recursive: true });
  let written = 0,
    kept = 0,
    moved = 0;
  for (const file of plan.files) {
    const preserve =
      file.action === "keep" ||
      (file.action === "conflict" && !replaceConflicts);
    if (preserve) kept++;
    if (!file.originalPath && (preserve || file.action === "unchanged"))
      continue;
    fs.mkdirSync(path.dirname(file.target), { recursive: true });
    fs.copyFileSync(
      preserve || file.action === "unchanged"
        ? file.originalPath
        : path.join(plan.staging, file.sourcePath),
      file.target,
    );
    written++;
    if (file.originalPath) {
      fs.unlinkSync(file.originalPath);
      moved++;
    }
  }
  for (const old of plan.obsolete || []) fs.unlinkSync(old.target);
  const oldDirectories = [
    ...new Set(
      [
        ...plan.files.map((file) => file.originalPath).filter(Boolean),
        ...(plan.obsolete || []).map((file) => file.target),
      ].map((file) => path.dirname(file)),
    ),
  ].sort((a, b) => b.length - a.length);
  const root = path.resolve(plan.destination);
  for (let directory of oldDirectories) {
    while (
      directory !== root &&
      directory.startsWith(root + path.sep) &&
      fs.existsSync(directory) &&
      !fs.readdirSync(directory).length
    ) {
      fs.rmdirSync(directory);
      directory = path.dirname(directory);
    }
  }
  if (options.writeManifest !== false)
    fs.writeFileSync(
      path.join(plan.destination, "extract-manifest.json"),
      JSON.stringify(plan.manifest, null, 2) + "\n",
    );
  // Promote a previously extracted folder to normal working source, if selected.
  const oldMarker = path.join(plan.destination, ".jade-extraction");
  if (fs.existsSync(oldMarker)) fs.unlinkSync(oldMarker);
  return { written, kept, ...(moved ? { moved } : {}) };
}

module.exports = {
  resolveExtractConfig,
  resolveExtractionDirectory,
  extractArguments,
  startExtract,
  planSourceUpdate,
  applySourceUpdate,
};
