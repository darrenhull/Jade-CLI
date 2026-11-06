# Engineering rules

- Follow TDD: add a meaningful failing test, verify the failure, implement the smallest change, and run the relevant regression suite. Record red/green evidence in the change description; do not manufacture failures.
- Keep one top-level class per source file, named for that class. Keep executable entry points thin. Functional modules are appropriate for cohesive stateless operations; do not invent classes merely to satisfy a naming convention.
- Store new tests under `tests/{Class}/{Class}_{method}_tests.test.js` (or `.test.ts` for TypeScript). For functional modules, use the module name in place of the class name. Group tests by the behavior or method under test.
- Apply SOLID: give each module one responsibility, inject process/filesystem/network dependencies at boundaries, and depend on public interfaces rather than another repository's source paths.
- Apply DRY: native compilation, loading, execution, shared-session ownership and shared release-update logic belong to the CLI/SDK. Consumers adapt those capabilities; they do not implement another compiler or protocol stack.
- Use the official MCP SDK for MCP protocol handling. Keep native actions in Jade's native runtime and owned C++ helpers.
- Preserve schema-qualified identities. Do not mix Source and Working database identities or infer source ownership from a class name alone.
- Default tests must not connect to an installed Jade database. Connected tests require explicit opt-in and disposable fixtures. Never use a developer's Source database as a test fixture.
- Keep Node/runtime/toolchain versions and dependency locks explicit. Release artifacts must pass version and checksum validation before publication or dependency updates.
- Never commit credentials, user configuration, databases, caches, execution logs or research notes. Track build-required source, tests, documentation, diagrams and pinned release dependencies.

## Code Review Rules

- Flag any path that lets build/test operations publish to Source, bypasses the shared broker's native ownership, or reuses inspection identities across database targets. Native work belongs in the existing Jade engine and owned helpers; adapters must use the shared public contracts.
- Flag changes that treat failed native loads as a successful reusable baseline, skip unpublished local dependencies, lose compiler/test failure details, or close a client's session while another owned operation is still using it.
- Flag releases or dependency updates that bypass version, checksum, lockfile-integrity or approved-default-branch checks. Preserve the committed CLI pin and independent-checkout builds. Tag release publication must remain separate from PR validation.
