# Jade tools CLI and local SDK

`jade-tools-cli` 0.1.0 runs the existing native Jade build/test engine independently of VS Code. It reuses the JavaScript orchestration and C++ helpers from Jade Native Runner 0.10.1. Jade owns compilation, object semantics and test lifecycle. A compatible installed Windows Jade runtime is required; vendor DLLs are not distributed here.

This is a local development package, not an npm-published release. The package includes the shared SDK under `sdk/`, native helpers and the `jade-cli` executable. `@jade-local-tools/sdk` is its internal SDK identity; no separate published dependency is required. The extension consumes a pinned packed copy without sibling-checkout imports.

## Commands

Unzip the Windows x64 distribution and run `jade-cli.exe help`. Keep its adjacent `bin` and `sdk` folders together with the executable. No separate Node.js installation is needed. Add that distribution folder to PATH to use `jade-cli` from another working folder. Developers can also run `node bin/jade-cli.js help` from a source checkout.

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

## Native ownership and private state

Compile/test loads local changes into an independent Working database. Tests execute afresh. RootSchema metadata is editor metadata, not executable state. Public publication commands are not exposed in this release.

An authenticated per-user named-pipe broker owns native processes and retains a warm compile/test session. Its credential directory has a Windows owner-restricted ACL; the local protocol also requires a random token. Clients do not receive tokens in output. Native jobs currently serialize globally, including independent Working bases: safe ownership takes precedence over parallel execution.

A held inspector/debugger helper lease returns an explicit busy result to another client rather than terminating debugging or waiting indefinitely. Close that session before compiling. The broker releases warm runtime handles before external tools. Surviving native PIDs block broker restart until ownership is resolved. It never terminates unrelated Jade applications.

Default broker state: `%LOCALAPPDATA%/JadeTools/broker-v1`. Set `JADE_TOOLS_STATE_DIRECTORY` to a separate private directory for isolated tests. `disconnect` releases retained native sessions; `shutdown` closes the broker and its owned helpers. These affect clients sharing that broker.

SingleUser Source must be available for initial backup. MultiUser Source uses native quiesced online backup: clients stay connected while updates briefly pause. Source server mode and original INI are not rewritten. Inspection is native read-only with security enforced. Test/application execution can mutate Working data.

## Extension and SDK integration

`jade-cli service --stdio` accepts JSONL:

```json
{"id":"1","command":"build","config":{}}
{"id":"2","command":"cancel","operationId":"1"}
```

Fill `config` with resolved settings above. Events are `{id,event:"output",text}` and `{id,event:"result",result}`. Build/test return the existing `startWorking` completion DTO, including `code`, `passed`, `cancelled`, `report`, diagnostics and test outcomes. Commands: `doctor`, `status`, `build`, `test`, `tests.list`, `source.search`, `inspect.catalog`, `inspect.instances`, `inspect.object`, `inspect.collection`, `inspect.release`, `inspect.close`, `disconnect`, `cancel`, `shutdown`.

Inspection sessions bind target/configuration identity. Close an old session before changing target; object identities and cursors never transfer between Source and Working. Client transport loss cancels its active jobs and closes its owned helper/inspection sessions. Successful idle compile/test state can remain warm in the broker.

Internal adapter gateways `native.run` and `helper inspect|debug|compiler [session.json]` preserve existing native protocols. Utilities are restricted to `jadclient`, `jadloadb`, `jdbutilb` and `jdbadmin` in the configured installation. These are trusted desktop interfaces, not unrestricted MCP tools or publication approval mechanisms. The helper gateway forwards native stdio under a broker lease. Existing version-pinned debugger ABI checks remain active. Arbitrary expressions, GUI hosting and unrestricted attach are not added.

The CommonJS SDK exports `JadeEngine` and `sourceFiles`; the package's `./client` export provides `openClient()` for shared broker access. Production multi-client native operations should use the broker. No MCP server is implemented here.

## Development

### Standalone Windows distribution

`npm run package:standalone` builds `dist/jade-cli-win-x64/jade-cli.exe` and `dist/jade-cli-win-x64.zip`. The executable contains the Node runtime using the official Node single-executable application mechanism; JavaScript/native helper sidecars remain in `bin` and `sdk`. The ZIP is the complete deployable folder. It requires the configured Jade installation, but no Node installation, package manager or VS Code.

Packaging requires the pinned Node v24.22.1 build runtime and development dependency `postject` 1.0.0-alpha.6. Install development dependencies from the lockfile before building. The matching Node runtime license is included. When Windows SDK signtool is available its original Node signature is removed before resource injection; the resulting executable is unsigned until a release signing step is added. A source package must run this build before `npm pack` to include the root `jade-cli.exe` used by the extension.

Set `JADE_CLI_STANDALONE_EXE` to the generated executable and run `node --test test/standalone.test.js` to verify help, a real source query, broker startup and shutdown with Node absent from PATH. That proof does not substitute for native Jade build/test controls.

The packaging implementation follows [Node v24.22.1 single-executable application documentation](https://nodejs.org/download/release/v24.22.1/docs/api/single-executable-applications.html). Generated EXE/ZIP files remain outside Git; native source, packaging scripts, lockfile and license notices are tracked.

`npm test` runs contract/process tests without opening Jade databases. `npm run check` verifies assets/manifests/links. Native connected controls use private databases separately. Rebuilding helpers requires the installed Jade SDK and C++ build tools. Do not commit vendor DLLs, databases, caches, credentials or generated reports.

Vendored dependencies retain their original licenses. No new license is assigned to legacy integration code or Jade vendor components. Distribution licensing remains to be decided.
