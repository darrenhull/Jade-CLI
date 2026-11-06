# Native CLI test coverage

Run from the Jade-CLI repository root on Windows with the Visual Studio 2022 x64 C++ toolchain and Windows SDK:

```powershell
./scripts/build-native-cli.ps1 -TestsOnly
./scripts/build-native-cli.ps1 -TestsOnly -TestFilter 'BrokerRuntime*'
```

The script discovers `*_tests.cpp` beneath `native-cli`, keeps assertions enabled, and fails on compilation errors or a nonzero test exit. The default suite uses temporary source/configuration files and isolated fake child processes. It does not connect to a developer's Jade database. The licensed-runtime proof below is opt-in and prints `SKIP` without its seed setting.

## Default C++ coverage

| Area and test files | Behavior checked |
| --- | --- |
| [CommandLine](CommandLine/): `prepare`, `edgeCases`, `emptyPaths`, `workingPath` | Nearest `jade.json`; configuration-relative versus invocation-relative paths; bare project and explicit-file selection; `--`; Windows case/path alias deduplication; Unicode and BOM input; unknown/missing options; mixed `--program`/positional selection; missing files, directory arguments, embedded NUL, malformed JSON and invalid workspace-root shapes. Empty scalar configuration paths remain unset so runtime defaults apply; non-string paths still fail. A relative `workingDatabaseDirectory` resolves against the configuration folder rather than the invocation folder. |
| [SourceCatalog](SourceCatalog/): `discover`, `buildInputs` | Recursive discovery independent of filenames/folder layout; dependency-directory exclusion; schema-qualified identities; active versus ignored unit tests; query paging; schema-only SCM input selection when there are no CLS files. |
| [RuntimeOperations](RuntimeOperations/): `dispatch` | Native implementation identity; utility allowlist; rejection of unknown commands/inspection targets; Source/Working target separation; failed inspector startup followed by safe close/disconnect. |
| [SourceModel](../engine/tests/SourceModel/): `parse`, `edge`, `scale` | Metadata extraction and source lines; schema ancestry; abstract classes; test flags/parameters; comments and string literals; malformed signatures and bounded metadata parsing. This is metadata discovery, not a replacement Jade compiler. |
| [ProjectModel](../engine/tests/ProjectModel/): `scan`, `edge`, `scope`, `case`, `snapshot`, `unicode`, `explicitFiles`, `stageBytes`, `missingHeader` | Native section staging; schema-qualified duplicate/cycle handling; dependency ancestry and overlapping roots; explicit/multiple file scope; exclusion of unrelated broken declarations; mixed-case CLS extensions; body-only versus signature/property shape changes; Unicode identities for not-yet-created paths, checked against Windows ordinal rather than linguistic/.NET text casing; configured external dependencies without sibling-directory discovery; selected ancestor dependencies without their schema header fail closed while CLS-only projects remain supported; original UTF-8 BOM/UTF-16 bytes preserved during incremental staging and stale snapshots rejected. |
| [InputInventory](../engine/tests/InputInventory/): `compare` | Unchanged inputs, method deltas, structural changes, partial versus full project scope, and removed-schema plans. |
| [NativeMigrations](../engine/tests/NativeMigrations/): `plan`, `edge` | JCF class rename parsing/planning; rename chains; idempotence; target collisions, missing targets and invalid/missing version input. |
| [DdxMerger](../engine/tests/DdxMerger/): `merge`, `edge` | Combining application definitions; complete-definition handling; schema/version mismatch, malformed XML and external-entity rejection. |
| [NativeReports](../engine/tests/NativeReports/): `tests`, `edge` | Individual pass/fail/skip results; selected methods; inherited assertion failure attribution from native stacks; missing/incomplete summaries; unrelated failures must not produce a misleading success. |
| [NativeProcess](../engine/tests/NativeProcess/): `run`, `failure`, `lifecycle`, `stream` | Windows quoting, child PID/exit status, separate stdout/stderr, split UTF-8, handle inheritance isolation, stdin EOF/closed writes, timeout/cancellation, missing/nonzero executables, and stopping a child with a blocked stdin writer. |
| [PrivateFile](../engine/tests/PrivateFile/): `remove` | A locked private configuration reports cleanup failure; removal succeeds when the owned lock is released. |
| [WorkingEngine](../engine/tests/WorkingEngine/): `execute`, `location`, `timeout` | Refuses an unsafe Source/Working identity and preserves the fixture Source control file. Empty optional settings resolve exactly like omitted settings; recreation changes the database identity while class/schema switching preserves the shared workspace database. Owned fake native utility and helper processes verify that timeouts retain `timedOut: true`, remain failures, and do not become cancellation. Real native compilation is covered separately below. |
| [BrokerRuntime](BrokerRuntime/): `request` | Eight concurrent cold callers share one broker PID; warm reuse; Unicode/progress; artifact mismatch; cancellation and error recovery; client-scoped inspection IDs/jobs; helper lease/busy behavior; actual live owned helper PIDs; clean helper and broker shutdown. |
| [BrokerRuntime](BrokerRuntime/): `transport` | Wrong-token and malformed raw-pipe rejection; 16 MiB packet limits; fragmented/coalesced frames; malformed service input followed by recovery; per-client queue bound; abandoned-client cancellation without cancelling another owner; stalled-reader recovery; split UTF-8/helper exit status; broker crash/restart; corrupt tokens and surviving-owner restart refusal; keeping helper ownership through client exit and releasing cleanup state only after actual child exit. |
| [BrokerRuntime](BrokerRuntime/): `lifecycle` | Queued inspection close followed by client disconnect releases the native session; pending and idle services exit with transport failure after broker loss while stdin stays open; an acknowledged shutdown exits promptly with success. |

The broker tests use real Windows pipes, processes, job objects and private TEMP state, with fake helper executables. They prove transport/ownership behavior without claiming to exercise Jade database semantics.

## Licensed Jade integration proof

[NativeProof_execute_tests.cpp](../engine/tests/NativeProof/NativeProof_execute_tests.cpp) requires a compatible licensed runtime at `C:\Jade2025`, the checked-in native helpers/DDX fixture, and a **private disposable seed database under TEMP**. The source fixture targets Jade 25.0.02; this is not a cross-version certification.

After provisioning that private seed and its matching parent `working.ini`:

```powershell
$env:JADE_CPP_TEST_SEED = 'C:\Users\your-user\AppData\Local\Temp\private-jade-seed\system'
try {
    ./scripts/build-native-cli.ps1 -TestsOnly -TestFilter 'NativeProof*'
} finally {
    Remove-Item Env:\JADE_CPP_TEST_SEED -ErrorAction SilentlyContinue
}
```

The proof rejects seeds outside TEMP and creates a separate Working database. It checks actual native compilation/execution of an unpublished class and test, repeated warm reuse, switching test classes and returning, an intentional assertion failure with its method identity, a bad-property compiler failure, and recovery. It also exercises DDX application loading, JCF rename/reuse, explicit and multiple-file builds, project-wide rejection of an unrelated broken class, and dependency-scoped recovery from a workspace-wide failure. The review regressions also exercise an actual UTF-16 incremental native load, verify the staged bytes, and compile/run a class referencing an unpublished configured dependency outside the workspace roots. Reports are written beneath its private proof directory.

Compiler and loader codes remain distinct: the bad-property case asserts a nonzero native loader status and compiler diagnostic **6026**. The observed loader status **8510** is not substituted for that diagnostic or presented as a passing test. It is not a claim that all compiler failures use the same loader status.

## Release verification

These PowerShell tests are separate from the C++ test discovery above:

```powershell
./tests/NativeDistribution/NativeDistribution_package_tests.ps1
./scripts/smoke-native-cli.ps1
```

| Test group | Behavior checked |
| --- | --- |
| [NativeDistribution](../../tests/NativeDistribution/) | Complete ZIP/checksum creation, tag/version mismatch and tampered runtime assets. Its package fixtures are not executable CLI integration tests. |
| [Portable CLI smoke](../../scripts/smoke-native-cli.ps1) | Copies the built native distribution outside the checkout, removes other runtimes from PATH, and runs help, doctor, source discovery, broker status and shutdown from that private copy. |

Release tests validate CLI artifacts and command behavior. They do not certify every Jade debugger ABI or licensed runtime version.

## Limits of the evidence

- `SourceModel_scale_tests.cpp` repeatedly parses metadata for **30,000 small in-memory class sources** and checks a 512 KiB malformed signature. It does **not** benchmark 30,000 disk files, dependency graphs, native compilations or tests. Timing output is a local measurement, not a production throughput guarantee.
- Fake-process tests cover Windows lifecycle failures but cannot certify all licensed Jade versions, database sizes, remote/multiuser environments or native crash modes.
- Hosted default CI does not claim the licensed proof ran when the seed variable is absent. Connected results must identify the actual private fixture/runtime used.
- Source publication and arbitrary attach/expression debugging are not established by these tests.
