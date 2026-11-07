# Jade CLI status

Implementation checkpoint: native CLI 0.4.1, focused identity PR [#8](https://github.com/darrenhull/Jade-CLI/pull/8), rebased onto approved main `b4d0b02` (Workspace PR7).

## T01 — Native Workspace execution

PR7 merged into protected main. Final feature head `f0dc9b8` passed hosted run42320075398, unit regressions and the actual native scoped preparation/deletion/history proof. All seven review findings were addressed. Automatic main release run42324831724 succeeded. Official v0.4.0 was published from b4d0b02; downloaded ZIP/EXE checksums, all seven manifest runtime files, tag/main ancestry and the version-matched portable no-Node smoke were independently verified.

Workspace execution uses Jade's native compiler/runtime against private Working databases. Scope includes schema-only/resource inputs, declared imports and durable managed history. Missing managed headers fail closed even when every imported schema file has disappeared. Complete local class removal and unrelated-schema preservation are covered by connected tests.

Workspace limits remain: void execution with independent locals, fixed RootSchemaApp context, bounded output, and committed Working data that can persist. There is no automatic rollback, GUI hosting, interactive input, custom application context or Workspace debugger. Native console encoding may lose unsupported output characters; source positions remain Unicode. Resource handoff does not override Jade's documented locale-loading behavior.

## T03 — Native Jade object identities

- One shared codec returns fixed RootSchemaApp `getObjectStringForObject` output unchanged and parses with fixed primitive `String.asObject`. Existing public identity fields use native values; packed hexadecimal input remains input-only compatibility.
- `oidFormat: jade-native` appears in the distribution manifest, doctor capabilities, helper ready packets and inspection response bodies. Native existence/kind checks and authorization remain authoritative.
- Actual native display and doctor-capability tests were RED before implementation and GREEN afterward. Native persistent, transient, shared-transient and 3/5-part subobject roundtrips, malformed input rejection, shared inspector output paths and unchanged primitive strings pass.
- Actual private Source/Working service lookups and a post-preparation SDK repeat pass. Explicit caller readOnly=false still yields native DB_READ_ONLY usage1. Inspector targets remain read-only; compile/test/Workspace execution retain their separate private Working write semantics.
- The pre-rebase 51-executable default suite, package validation and portable no-Node smoke passed. Pre-rebase head151352f passed hosted run42319233768. Rebased 0.4.1 passed all64 default executable tests, connected SDK and Source/Working service proofs, workflow/publisher contracts, package/manifest checks and portable no-Node smoke. Latest-head hosted CI and review remain required.
- Real MCP verification passed39 calls/92 native canonical identities across both targets, including native-property-derived nonempty collections, references, native ID errors/recovery and scoped cursor binding. Extension verification is tracked by its owning agent; final consumers must pin an official verified release.

### Fixture isolation correction

An early consumer copy retained the original private INI journal paths. A repeated test exposed native3014; the old private fixture is preserved, and those initial consumer isolation claims were invalidated. Production Source was untouched. Three replacements were generated through native verified backup and restore, with every mutable INI directory validated under its own root before sign-on. Consumers received separate fixtures and were instructed not to copy or rename their database/INI. Fresh native SDK, Source/Working and MCP checks passed afterward. The fixture preflight rejects external, relative and default journal/transient directories and handles LF/CRLF.

### Post-review proof-entry safeguards

Both C++ identity proof entry points now run the same canonical owned-directory preflight before any Runtime, backup or sign-on; direct invocation cannot bypass the PowerShell guard. Native-open-free regressions rejected copied INI paths, missing/default/relative/duplicate settings, old/missing staged manifests and tampered helpers. A valid staged manifest passed. The exact documented build-script invocation passed actual Source/Working inspection with cleanup, and direct SDK/CLI child entry tests rejected unsafe paths before native work; direct SDK valid-seed roundtrips also passed. The connected service proof requires an explicit version-matched, checksum-verified JADE_CPP_TEST_DISTRIBUTION and has no checkout or older-helper fallback. Latest-head CI must pass after these review fixes.

## Read-only data-tools boundary

`RuntimeOperations` forces readOnly for Source and Working inspection. The native inspector independently signs on to RootSchema/RootSchemaApp with fixed database usage1 (DB_READ_ONLY); caller configuration cannot enable writes. Sessions are bound to their selected target/configuration. Inspection dispatch is fixed to catalog, instances, object, collection, cursor release and close; caller command/id fields are overwritten.

Native IDs are bounded to512 UTF-8 bytes without ASCII controls, then parsed by Jade (or decoded as legacy compatibility input) and checked natively. Stored properties use native property access; mapped properties are skipped. Application-defined allowedToInspect and configured development-security authorization hooks remain intact. Iterators and licence objects are transient resources. Cursor/session tokens are opaque and scoped, not globally unique object identities.

The boundary is read-only persistent data access, not an OS sandbox or a promise that trusted native/authentication callbacks have no external effects. No production Source mutation test was performed. Native authority: [Jade Object Manager process opening](https://secure.jadeworld.com/developer-centre/JADE2018/OnlineDocumentation/content/resources/objectmanager/ch3jadeapi/opening_a_process.htm).

## Pending release gates

- Complete rebased 0.4.1 regression/CI/review gates, protected approval and official release.
- Verify official archive manifests/helper hashes and consumer pin/package integration. Local candidates are not published releases.

The coordinator owns Ready, owner review, approval, merge and release ordering. WIP remains Draft.

## T13 — Relocation

Relocation is paused during PR handoffs. Only the coordinator moves repositories/workspaces to C:\Git, after every agent reports owned processes stopped. Preserve user source, Source databases, private Working state and blocked-cleanup fixtures. Repair Git worktree metadata and absolute configuration paths after relocation. Workspace-root path changes affect Working identities; retain prior state rather than deleting it or overwriting Source. This is development status, separate from consumer release notes.
