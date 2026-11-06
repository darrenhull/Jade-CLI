# Jade CLI status

Implementation checkpoint: CLI 0.4.0, PR [#7](https://github.com/darrenhull/Jade-CLI/pull/7). Verified implementation head: `116279e` (this documentation checkpoint may follow it).

## Delivered

- Native signature-free Workspace execution through terminal and JSONL, using private Working databases and Jade's compiler/runtime.
- Exact snapshot diagnostics, bounded output, cancellation, timeout and recovery; Source publication remains separate.
- Schema-only/resource input preparation, one-pass visible schema scope, complete scoped inventories and unchanged-input reuse.
- Native class removal under complete schema declarations; partial/missing managed headers fail closed.
- Locally imported class files cannot bypass missing schema headers.
- Durable managed identities survive failed native preparation and same-path class/schema replacement, independently of reusable build claims.
- Version-specific consumer release notes describe the actual new scratch execution feature; development/review details stay here and in PR documentation.

## Verification

- Full local default regression: 60 executable tests completed, including four explicitly skipped licensed-runtime tests, before the final review additions. Later additions passed focused inventory, projection and WorkingEngine suites.
- Actual Jade private-seed proof passed: zero-CLS schema creation, supplied section input/native load, unchanged reuse, class creation/deletion, native6023 after deletion, partial/missing-header rejection and unrelated schema preservation.
- Extended native proof passed: missing imported header rejection, owned native rejoin timeout, removal of all local schema inputs, a fresh engine rejecting stale execution from durable history, and unrelated schema output73 afterward.
- Final connected fixture removed after disconnect; no owned native processes remain. Earlier blocked-cleanup fixtures are retained untouched.
- Portable native executable smoke, configuration, packaging, publisher contracts and actionlint passed. Publisher tests cover explicit feature notes, missing-note rejection, immutable published releases and draft resume.
- Final required hosted checks are linked on PR7; implementation head116279e started [run42313020516](https://github.com/darrenhull/Jade-CLI/actions/runs/42313020516). The latest PR head, including this checkpoint, must be green before readiness.
- Existing six Codex findings are resolved after verified fixes. Any later findings must be checked before readiness.

## Pending

- Final-head hosted checks, readiness and owner approval; the coordinator owns Ready/review/merge actions.
- Automatic protected-main CLI0.4 release and verification of the published archive/checksums.
- Consumer pin updates and final extension integration/release use the official release, not an unreviewed local candidate.

## Limits

Workspace mode is void with independent locals and fixed RootSchemaApp context. Committed Working data can persist. There is no automatic rollback, GUI hosting, interactive input, custom application context or Workspace debugger. Native console encoding can lose unsupported characters; source positions remain Unicode. Resource handoff does not override Jade's documented locale-loading behavior. The 30,000-class projection benchmark is in memory, not a disk/native compilation benchmark.

## Relocation handoff

The coordinator will move repositories/workspaces out of OneDrive to `C:\Git` only after every agent has stopped. Do not move files from this branch while another agent or owned native process is active.

Preserve Git worktree metadata and repair/verify worktree paths after relocation. Update absolute workspace/configuration references before running. Working identities depend on source-root paths: retain prior private Working databases/caches and data; do not delete them or overwrite Source during relocation. The installed Jade runtime/database is outside these repository moves.

CLI worktrees are the main `Jade-CLI` checkout on `codex/agent/structural-diagnostics`, plus earlier init and structural-hotfix worktrees under `development`. Those earlier branches were merged; verify their own clean state before cleanup. The F5 extension worktree is owned by the UI agent after handoff, not by this CLI task. Two blocked-cleanup TEMP fixtures must remain untouched; the coordinator has their exact paths.
