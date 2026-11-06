# Native Jade CLI engineering and review guidance

## Architecture and responsibilities

This repository owns the native C++ jade-cli executable, per-user broker, build/test orchestration, owned C++ helpers and CLI release/packaging automation. Jade owns compilation, object semantics, persistence and native execution. Protocol clients use the JSONL service; the CLI must not embed or launch Node, JavaScript or Python. Keep this repository focused on the CLI and its integration with Jade.

- Preserve coordinated native/SingleUser ownership across terminal and JSONL clients through one broker.
- Review discovery, dependency closure, schema ancestry, structural changes and successful-input inventories together, including test-class switches and failure recovery.
- Keep ABI/version assumptions explicit. Do not guess native signatures or replace native semantics with a permissive custom checker.
- Keep inspection read-only; bind sessions/OIDs/cursors to their selected targets and retain native resources/configuration until actual process exit.
- Check command and JSONL contracts, framing, broker authentication/fingerprints, bounded output and cancellation across concurrent clients.
- Native build and release automation must work without a sibling checkout and must not expose arbitrary commands.

## Engineering approach

- Understand the change, its callers and its runtime/data boundaries before editing. Inspect implementation and tests; do not infer behavior from names alone.
- Use TDD: add a meaningful failing test, verify the intended failure, implement the smallest correct change, then run relevant regressions. Record red/green evidence in the PR.
- Keep one top-level class per source file, named for that class. Keep executable entry points thin. Cohesive stateless functions are appropriate; avoid artificial classes.
- Store native tests under tests/{Class}/{Class}_{method}_tests.cpp and PowerShell automation tests under tests/{Module}/{Module}_{method}_tests.ps1. Keep tests grouped by their owning class or module; share realistic fixtures. Do not disable assertions in test builds.
- Apply SOLID at actual boundaries: separate transport, configuration, orchestration, native execution and presentation; inject process/filesystem/network dependencies where tests need control.
- Apply DRY within the CLI: share process ownership, security, configuration, sessions and dependency selection instead of implementing separate command and service engines.
- Prefer maintained libraries and official SDKs for established protocols. Keep custom glue small and readable; avoid speculative abstractions and catch-and-ignore fallbacks.
- Run focused tests first, then relevant regression/build/package checks. Default tests must not open a developer's Jade database; connected tests require explicit opt-in, disposable validated fixtures and cleanup of owned resources only.
- Review the final diff for unintended files, secrets, stale comments and behavior changes. Document the final behavior, validation and practical limitations.

## UI evidence

- Every UI change must include a screenshot of the updated, running interface. Capture the actual implementation after the change, not a mockup or an earlier version.
- Visually inspect the screenshot and include it in the PR and the user-facing completion report, with enough context to identify the changed view or control. Cover materially different changed states where one screenshot is insufficient.
- If the UI cannot be run or captured, report the blocker explicitly and leave visual verification outstanding; do not claim it is complete.

## Correctness and compatibility

- Preserve public commands, JSONL/event/result contracts, native error codes, cancellation, diagnostics, source locations and individual test outcomes. Incomplete or failed operations must not appear successful.
- Resolve configuration predictably and preserve schema-qualified identities. A same-named class in another schema is a different declaration.
- Preserve native source formats, encoding and meaningful ordering. Avoid broad transformations and invented Jade metadata.
- Build/test against saved local files and their dependencies, including unpublished classes. Establish reusable baselines only after successful native loads and invalidate stale claims after failures or structural changes.
- Handle Windows paths, quoting, case behavior, Unicode, locks and process-exit timing explicitly. Packages must not accidentally depend on a developer checkout, PATH or current directory.

## Security, data and lifecycle

- Keep Source and Working targets distinct. Build/test/debug writes belong in Working; Source publication requires a concrete reviewed plan and authorization.
- Route native work through shared ownership and allowlisted tools. Never expose arbitrary shell/native execution or secret-bearing configuration through an adapter.
- Validate source paths, archive members, arguments, OIDs, cursors and network payloads appropriately. Keep object/cursor identities bound to their database and session.
- Keep secrets out of Git, logs and public artifacts. Restrict private configuration access; preserve it while the native process owns it and clean it up after actual exit.
- Check startup races, cancellation, connection loss, crashes, shutdown and partially completed writes. Settle pending promises, cancel owned work and preserve safe recovery without killing unrelated processes.
- Default to minimal permissions. Verify trusted release origin, version, archive safety, checksums and lockfile integrity before installing or executing an update.

## Performance and operability

- Measure cold and warm paths separately; distinguish database preparation from compilation and execution before claiming improvements.
- Reuse valid native sessions and successful inventories. Avoid repeated full scans, duplicate parsing, per-item process/network calls and unbounded work on hot paths.
- Assume 30,000+ classes: bound concurrency, paginate results, limit retained output and avoid sending full source graphs through UI/transport responses.
- Provide actionable errors and progress, retain the original failure cause, clean up owned resources and make retries safe. Do not hide backend failures with an alternate implementation.

## Builds, releases and maintenance

- Open work-in-progress PRs as drafts and return them to draft when further changes are needed. Keep them draft until implementation, required checks and review findings are complete. A code push must not automatically mark a PR ready or queue its merge; readiness is a separate reviewed transition. Required owner approval and protected-branch checks still apply.

- Independent checkouts must build from committed native inputs and declared toolchain/dependency versions.
- Exclude credentials, user configuration, databases, caches, logs and research notes from Git. Track requested diagrams and build-required source, tests and notices.
- Pin toolchains/dependencies and test the actual portable artifact outside the checkout without Node on PATH.
- Validate PRs without publishing. Successful protected-main builds may publish a new version automatically; published versions stay immutable. Tags must match native versions and reviewed-main commits. Queue squash auto-merge only behind required owner approval and checks; never approve PRs or bypass branch protection on the user's behalf.
- Keep README, examples, capabilities and diagrams aligned with implemented behavior. Distinguish hosted CI from connected checks requiring the licensed Jade runtime.

## Code Review Rules

These rules are not exhaustive. Review the complete change and its affected callers for correctness, regressions, security, reliability, maintainability and performance. Do not restrict findings to listed conventions. Prefer concrete, consequential issues with a trigger, impact and location over speculative or style-only findings.

- Trace success, failure, cancellation and repeated operations across boundaries. Check partial failures, races, resource leaks, stale state and recovery.
- Ask whether the tests detect the actual failure, especially where native behavior differs from a mock. Flag misleading green checks and missing meaningful behavioral coverage.
- Check compatibility of the native executable, helpers, Jade ABI/runtime assumptions and release artifacts.
- Check that Source cannot be changed by build/test/read-only inspection and that native ownership and target identity cannot be bypassed or confused.
- Check performance evidence and ensure ordinary navigation, single-file operations and paged lookups do not unexpectedly trigger expensive project-wide work.
- Check release/update automation for unreviewed publication, unverified downloads, stale integrity and silently skipped failures.

## Automated PR comments

Post automated PR comments through GitHub Actions or an authorized GitHub App so they use the bot identity. Include attribution that Codex prepared the comment on behalf of @darrenhull. The user-authenticated GitHub connector posts as the user; do not use it for future automated comments or imply that it is a bot. If a bot-backed path is unavailable, report that limitation instead of impersonating a bot.

Do not request owner review or enable auto-merge while a pull request is Draft. Request owner review only after implementation, checks, and review fixes are complete and the pull request is explicitly marked ready.

