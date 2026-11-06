# Jade CLI — native C++

`jade-cli.exe` 0.4.0 is a native Windows C++ executable. Command handling, configuration discovery, the shared broker, dependency selection, Working database preparation and build/test orchestration run in C++. There is no embedded Node runtime, JavaScript engine or Python process in the CLI.

Jade's installed native tools and object manager perform compilation and execution. The distribution includes our native helper executables, not Jade vendor DLLs or a database. A compatible licensed Windows Jade installation is required for connected operations. Terminal commands and JSONL service requests use the same native implementation.

## Commands

Unzip the Windows x64 distribution and run `jade-cli.exe help`. Keep its adjacent `native` folder with the executable. Add the distribution folder to PATH to use `jade-cli` from any working folder. Neither users nor CLI builders need Node or npm.

```text
jade-cli init --profile demo --install C:\Jade2025
jade-cli doctor --json
jade-cli build --json
jade-cli test .\FishTest.cls --json
jade-cli workspace run .\scratch.jade --schema ExampleSchema --json
jade-cli tests list --config jade.json --json
jade-cli source search --config jade.json --query Fish --json
jade-cli inspect catalog --config jade.json --target working --json
jade-cli inspect instances --config jade.json --target working --args "{...}" --json
jade-cli status --json
jade-cli disconnect --json
jade-cli shutdown --json
```

The CLI automatically finds the nearest `jade.json` in the current folder or its parents. Paths inside that configuration are relative to the configuration file; source arguments are relative to your terminal's current folder. `--config` selects a different configuration explicitly.

```powershell
jade-cli build .\Fish.cls .\Hampster.cls
jade-cli test .\FishTest.cls .\HampsterTests.cls
```

Run `jade-cli build` without filenames to scan the configured project, including newly added files. It ignores any legacy selected `program` setting and compares current source against the last successful Working build. Unchanged native inputs are reused; method-only edits can use incremental CLS loads. New classes, schema declarations and other structural changes can require a broader native project load to preserve dependencies. A failed load invalidates reusable build claims while retaining managed source identities, so retries cannot skip the failure or execute a removed local schema through stale Working definitions. This is independent of Git and does not publish to Source. Bare project builds currently require one workspace root.

Multiple build files are supplied together to one native project build. Multiple test files run sequentially through the same shared session, with a result for each file and a failing exit status if any fails. Duplicate source arguments are removed. Quote paths containing spaces. Pass explicit filenames; wildcard expansion is not implemented. These commands use the isolated Working database and do not publish to Source.

Structural schema compiler failures include native source diagnostics as well as their Jade error code. The helper captures only the current load's bounded compiler log. Unambiguous generated source lines map back to their original local file with the native position/length; ambiguous mappings retain the actual generated file and line rather than guessing a location. A successful retry clears the failed load's diagnostics.
`--args` takes a JSON object with native inspection fields such as `classOid`, `oid`, `cursor` and `limit`. Pages are bounded to 100. Inspection defaults to Working; `--target source` explicitly selects the original database. Use the persistent service for cursor paging across requests.

## Jade Workspace execution

`jade-cli workspace run scratch.jade --schema ExampleSchema` executes a signature-free native Jade Workspace, for example:

```jade
vars
    value : Integer;
begin
    value := 40 + 2;
    write value;
end;
```

The CLI prepares visible saved schema source in the private Working database before executing the supplied script. Saved headers and resources are included even when a schema has no class files. Within selected filesystem schemas, source is authoritative: removed local classes are removed from Working on the next preparation when the schema header declares completeDefinition; a partialDefinition header fails closed for class deletion; unrelated schema inventories are retained. Removing an entire managed schema header fails closed and requires restoring the header or an explicit complete project build. Each invocation has independent local variables. Committed persistent changes affect Working and can survive later runs; this is not an automatic rollback sandbox. The Source database is not the execution target and nothing is published. RootSchema script execution is rejected by Jade, so choose a user schema for compilation. Execution uses the fixed native RootSchemaApp application context. Custom application contexts are not exposed. `write` output is captured; interactive input, GUI code and Workspace debugging are not exposed. The initial execution mode is void, so print values rather than returning a value.

The JSONL operation is `workspace.run`, with `config`, exact `sourceText`, an optional `source` URI label, and optional zero-based `lineOffset`/`columnOffset` for a selected source snapshot. The result preserves native compiler codes and UTF-16 positions/lengths in `report.diagnostics`, with one-based source line/column, plus `report.output`, execution state and Working identity. Clients must inspect `passed` and diagnostics: a handled Jade compiler error can accompany a native process exit code of zero. Cancellation/timeout stop the owned process; partial redirected output can remain buffered until exit.

Workspace input is limited to 64 KiB of UTF-8 text; result output is bounded to 256 KiB and indicates truncation. Standard input is closed: a Jade `read` receives EOF/default input instead of asking the user, so code containing `read` may still complete with an empty/default value. The verified `jadclient` runtime emits redirected output using the Windows ANSI code page, which the CLI decodes before returning UTF-8 JSON. `outputEncoding` identifies that code page. Jade can replace characters unavailable in that encoding, so this path does not promise lossless Unicode console output. The source snapshot and native diagnostic positions remain Unicode.

Structural load errors also use Jade's per-load compiler log. An unambiguous source line is mapped back to its original local file. If a generated line cannot be identified uniquely, the diagnostic retains the actual generated file and native location rather than guessing a user source line.

## Project setup

Register a machine connection once, from your project folder:

```powershell
jade-cli init --profile demo --install C:\Jade2025
jade-cli build
jade-cli test .\FishTest.cls .\HampsterTests.cls
```

`init` creates a minimal `jade.json` suitable for source control:

```json
{ "connection": "demo" }
```

The installation path lives in your private `%LOCALAPPDATA%/JadeTools/connections.json`, outside the repository. The CLI infers the Source database as `<installPath>/system`, its INI as `<databasePath>/jade.ini`, the workspace root as the folder containing `jade.json`, and private Working storage as `%LOCALAPPDATA%/JadeTools/projects`. Working identities include workspace roots, so separate projects keep separate Working databases. Initialization writes configuration only; it does not connect, extract, compile or publish source.

A missing `jade.json` is initialized automatically by `build`, `test` or `workspace run`. With one saved connection it is selected automatically. With several, use `--profile demo`; an interactive terminal can choose from a list. With no saved connection, use `--install`, set `JADE_INSTALL` (or `JADE_HOME`), or enter the installation folder when prompted. Piped input, JSONL services and CI never prompt: ambiguous or missing setup returns a clear configuration error. An explicitly named missing `--config` file is an error, rather than an implicit new project.

Reuse the connection in another project:

```powershell
cd C:\repos\another-project
jade-cli init --profile demo
jade-cli build
```

On a different machine, a checked-in `{ "connection": "demo" }` can be connected with `jade-cli init --install C:\Jade2025`. Existing project files are preserved byte-for-byte. Existing profile names cannot silently be repointed to another database: register a different name for a different connection.

Override a machine's Source location when creating a profile, including MultiUser mode when required:

```powershell
jade-cli init --profile shared --install C:\Jade2025 --database D:\JadeData --ini D:\JadeData\jade.ini --server multiUser
```

Project overrides remain optional and relative to `jade.json`:

```json
{
  "connection": "demo",
  "schema": "ExampleSchema",
  "workspaceRoots": ["schemas"],
  "databasePath": "../another-base"
}
```

Command-line `--profile`, `--install`, `--database`, `--ini`, `--server` and `--schema` override the selected configuration for that invocation. Source arguments remain relative to the terminal folder. `--workspace` selects a workspace root. Full legacy JSON configurations with explicit `installPath`, `databasePath`, `iniPath`, `storagePath` and other settings continue to work. Optional `files`, `methods`, `testDependencyScope`, `workingDatabaseDirectory`, `workingDatabaseGeneration` and `timeoutMilliseconds` retain their engine meanings. Credentials are session inputs, not checked-in configuration.

For isolated automation, `JADE_TOOLS_CONNECTIONS_FILE` selects an absolute private profile-file path. For example:

```powershell
$env:JADE_TOOLS_CONNECTIONS_FILE = "$env:LOCALAPPDATA\JadeTools\ci-connections.json"
$env:JADE_TOOLS_STATE_DIRECTORY = Join-Path $env:TEMP ("jade-ci-" + [guid]::NewGuid().ToString("N"))
jade-cli init --profile ci --install C:\Jade2025 --json
jade-cli build --profile ci --json
```

The broker inherits these environment settings when it starts. Choose a fresh private `JADE_TOOLS_STATE_DIRECTORY` for isolated automation, as above, or run `jade-cli shutdown` before changing the profile-file location. Changing an environment variable does not reconfigure an already-running broker.

CLI results preserve native errors and diagnostics. Progress goes to stderr; final JSON goes to stdout. Exit codes are 0 success, 1 operation failure, 2 transport/configuration failure, and 130 reported cancellation.

When a headered project selects a dependency from another schema, that schema must also have a `.scm` or `schemaDefinitions.sec` supplied in the workspace or `config.files`; otherwise the build fails before loading, rather than using a stale database definition.

## Native ownership and private state

Compile/test loads local changes into an independent Working database. Tests execute afresh; reusing a native session does not reuse old test results. Public publication commands are not exposed in this release.

An authenticated per-user named-pipe broker owns native processes and retains a warm compile/test session. Its credential directory has a Windows owner-restricted ACL; the local protocol also requires a random token. Clients do not receive tokens in output. Native jobs currently serialize globally, including independent Working bases: safe ownership takes precedence over parallel execution.

A held inspector/debugger helper lease returns an explicit busy result to another client rather than terminating debugging or waiting indefinitely. Close that session before compiling. The broker releases warm runtime handles before external tools. Surviving native PIDs block broker restart until ownership is resolved. It never terminates unrelated Jade applications.

Default broker state: `%LOCALAPPDATA%/JadeTools/broker-v2-native`. Set `JADE_TOOLS_STATE_DIRECTORY` to a separate private directory for isolated tests. `disconnect` releases retained native sessions; `shutdown` closes the broker and its owned helpers. These affect clients sharing that broker. The native broker has a separate namespace from the archived Node implementation.

SingleUser Source must be available for initial backup. MultiUser Source uses native quiesced online backup: clients stay connected while updates briefly pause. Source server mode and original INI are not rewritten. Inspection is native read-only with security enforced. Test/application execution can mutate Working data.

## JSONL service

`jade-cli service --stdio` accepts JSONL:

```json
{"id":"1","command":"build","config":{}}
{"id":"2","command":"cancel","operationId":"1"}
```

`config` accepts a private connection reference such as `{ "connection": "demo", "workspaceRoots": ["C:/repos/example"] }`, installation-only settings with inferred defaults, or the existing fully resolved settings. Native operations require a configured connection. Service setup is noninteractive; `{ "command": "cli", "operation": "init", "argv": ["--profile", "demo"], "cwd": "C:/repos/example" }` initializes a project without opening the database. Events are `{id,event:"output",text}` and `{id,event:"result",result}`. Build/test results include `code`, `passed`, `cancelled`, `report`, diagnostics and test outcomes. Commands: `doctor`, `status`, `build`, `test`, `workspace.run`, `tests.list`, `source.search`, `inspect.catalog`, `inspect.instances`, `inspect.object`, `inspect.collection`, `inspect.release`, `inspect.close`, `disconnect`, `cancel`, `shutdown`.

Inspection sessions bind target/configuration identity. Close an old session before changing target; object identities and cursors never transfer between Source and Working. Client transport loss cancels its active jobs and closes its owned helper/inspection sessions. Successful idle compile/test state can remain warm in the broker.

Native gateways `native.run` and `helper inspect|debug|compiler [session.json]` preserve Jade's native protocols. Utilities are restricted to `jadclient`, `jadloadb`, `jdbutilb` and `jdbadmin` in the configured installation. These are trusted local interfaces; callers remain responsible for authorizing Source writes. The helper gateway forwards native stdio under a broker lease. Version-pinned debugger ABI checks remain active. Arbitrary expressions, GUI hosting and unrestricted attach are not added.

Protocol clients launch `jade-cli.exe service --stdio`. `{ "command": "cli", "operation": "build", "argv": [], "cwd": "C:/repos/example" }` uses the same configuration discovery and command handling as the terminal. `prepare` returns resolved configuration metadata without connecting to a database.

## Development and verification

Build with the Visual Studio 2022 x64 C++ toolchain and Windows SDK:

```powershell
./scripts/build-native-cli.ps1
```

The script compiles the native library, runs grouped C++ tests, and builds `dist/jade-cli-win-x64/jade-cli.exe` with a static C++ runtime. It discovers the installed Visual Studio toolchain automatically. `-TestsOnly` runs tests; `-TestFilter 'BrokerRuntime*'` selects a group. No package manager or downloaded C++ dependency is needed: the existing MIT-licensed nlohmann/json header is vendored.

Tests cover configuration and positional arguments, schema-qualified discovery, process quoting/cancellation, source staging, diagnostics, warm sessions, broker startup races, authentication, client ownership, helper leases and recovery. The [test coverage matrix](native-cli/tests/README.md) identifies each test group and its limits. Default tests do not open a developer database. The explicit `JADE_CPP_TEST_SEED` connected proof accepts a private disposable TEMP database and validates actual Jade compilation and test execution. Hosted CI does not claim connected coverage without the licensed runtime.

Rebuilding Jade helper executables also requires the installed Jade SDK. No Jade vendor DLLs, databases, credentials or generated evidence belong in Git. The previous Node implementation is preserved on the `codex/agent/ci-releases` branch (with a local `codex/node-cli-baseline` checkpoint) for comparison; it is not included in the native distribution.


## GitHub builds and releases

Windows CI builds and tests the native executable, verifies a portable copy outside the checkout, and creates `jade-cli-win-x64.zip`, `jade-cli.exe` and `SHA256SUMS`. The ZIP contains the complete native runtime and its hash manifest; the standalone EXE still needs the adjacent native helpers for connected operations. There is no npm runtime release.

Pull requests run validation once. After an approved merge to `main`, a successful build publishes a new native version automatically; an already-published version remains unchanged. Bump the native version to publish a new CLI release. Only protected-main pushes publish; tag pushes do not start a competing publisher. Before retaining an existing published version, the release job verifies its tag is in reviewed `main` history and checks its downloaded EXE, ZIP, checksums and native manifest. The release job verifies downloaded artifacts and finishes uploading a draft before publication. `SHA256SUMS` covers the EXE and ZIP; `distribution.json` records the hashes of the executable and its runtime files.

Bot PR creation runs separately from the full build. Squash auto-merge waits for the required owner approval, checks and resolved discussions; feature branches are deleted after merge. GitHub may require approval to start workflows on a newly bot-created PR.


## Architecture diagram

![Architecture](docs/architecture.drawio.svg)

[Edit the diagram in draw.io](docs/architecture.drawio). The SVG is the rendered companion to the editable source.
