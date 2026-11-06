# Jade CLI — native C++

`jade-cli.exe` 0.2.0 is a native Windows C++ executable. Command handling, configuration discovery, the shared broker, dependency selection, Working database preparation and build/test orchestration run in C++. There is no embedded Node runtime, JavaScript engine or Python process in the CLI.

Jade's installed native tools and object manager perform compilation and execution. The distribution includes our native helper executables, not Jade vendor DLLs or a database. A compatible licensed Windows Jade installation is required for connected operations. Terminal commands and JSONL service requests use the same native implementation.

## Commands

Unzip the Windows x64 distribution and run `jade-cli.exe help`. Keep its adjacent `native` folder with the executable. Add the distribution folder to PATH to use `jade-cli` from any working folder. Neither users nor CLI builders need Node or npm.

```text
jade-cli doctor --config jade.json --json
jade-cli build --json
jade-cli test --config jade.json --json
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

Run `jade-cli build` without filenames to scan the configured project, including newly added files. It ignores any legacy selected `program` setting and compares current source against the last successful Working build. Unchanged native inputs are reused; method-only edits can use incremental CLS loads. New classes, schema declarations and other structural changes can require a broader native project load to preserve dependencies. A failed load invalidates its build claims, so retrying cannot silently skip the failure. This is independent of Git and does not publish to Source. Bare project builds currently require one workspace root.

Multiple build files are supplied together to one native project build. Multiple test files run sequentially through the same shared session, with a result for each file and a failing exit status if any fails. Duplicate source arguments are removed. Quote paths containing spaces. Pass explicit filenames; wildcard expansion is not implemented. These commands use the isolated Working database and do not publish to Source.
`--args` takes a JSON object with native inspection fields such as `classOid`, `oid`, `cursor` and `limit`. Pages are bounded to 100. Inspection defaults to Working; `--target source` explicitly selects the original database. Use the persistent service for cursor paging across requests.

Configuration is a JSON object using the existing resolved engine settings:

```json
{
  "installPath": "C:/Jade2025",
  "databasePath": "C:/Jade2025/system",
  "iniPath": "C:/Jade2025/system/jade.ini",
  "server": "singleUser",
  "schema": "ExampleSchema",
  "workspaceRoots": ["C:/repos/example"],
  "program": "C:/repos/example/schemas/ExampleSchema/types/ExampleTests.cls",
  "storagePath": "C:/Users/example/AppData/Local/JadeTools/example"
}
```

Use actual absolute paths. `program` selects the test/class context; optional `files`, `methods`, `testDependencyScope`, `workingDatabaseDirectory`, `workingDatabaseGeneration` and `timeoutMilliseconds` retain existing engine meanings. Credentials are session inputs, not checked-in configuration. CLI results preserve native errors and diagnostics. Progress goes to stderr; final JSON goes to stdout. Exit codes are 0 success, 1 operation failure, 2 transport/configuration failure, and 130 reported cancellation.

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

Fill `config` with resolved settings above. Events are `{id,event:"output",text}` and `{id,event:"result",result}`. Build/test results include `code`, `passed`, `cancelled`, `report`, diagnostics and test outcomes. Commands: `doctor`, `status`, `build`, `test`, `tests.list`, `source.search`, `inspect.catalog`, `inspect.instances`, `inspect.object`, `inspect.collection`, `inspect.release`, `inspect.close`, `disconnect`, `cancel`, `shutdown`.

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

Release tags must match the native version and refer to a commit on the reviewed `main` branch. The release job verifies downloaded artifacts before publishing. `SHA256SUMS` covers the EXE and ZIP; `distribution.json` records the hashes of the executable and its runtime files. No release or merge is performed by an ordinary build.

## Architecture diagram

![Architecture](docs/architecture.drawio.svg)

[Edit the diagram in draw.io](docs/architecture.drawio). The SVG is the rendered companion to the editable source.
