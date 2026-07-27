# Repository Guidelines

## Priorities

Build lightweight, offline-first MCP tools, primarily for LM Studio, on macOS, Linux, and Windows.

- Prefer the standard library, stdio or short-lived processes, and small model-facing schemas.
- Avoid cloud services, accounts, telemetry, runtime downloads, and unnecessary dependencies.
- Bound memory, output, recursion, collection sizes, file sizes, state, and execution time.
- Isolate platform-specific behavior, test each branch, and document offline preparation for pinned dependencies.

## Structure and workflow

Applications are `rooted-files-mcp/`, `godot-editor-mcp/`, and `unreal-editor-mcp/`. Keep each app self-contained; add shared code only when multiple apps need it.

Before changing an app, follow its `AGENTS.md` and `docs/workflow.md`, beginning at `docs/index.md`. Use the architecture and type indexes to identify the smallest relevant set of source, dependencies, tests, metadata, examples, roadmap/history, and documentation.

Keep one file per component under `docs/architecture/`, component-owned references under `docs/types/`, and an immediate relative-link `index.md` in every docs directory. Do not create a `CODE.md` monolith.

Executable source, schemas, metadata, runtime contracts, and behavioral tests define behavior. Documentation explains them and must not be executable input or a test fixture.

Keep responsibilities narrow and interfaces explicit. Do not include unrelated refactors without authorization.

## Security and MCP contracts

- Treat model input as untrusted; validate types, lengths, paths, operations, and encoded sizes.
- Confine filesystem access to configured roots and reject traversal and symlink escapes.
- Keep tools few, schemas compact, results bounded, and errors stable.
- Write protocol messages only to stdout; send diagnostics to stderr.
- Never commit secrets, tokens, or machine-specific paths.
- Test MCP initialization, `tools/list`, and `tools/call` with LM Studio-compatible framing.

## Changes and releases

- Test normal, invalid, limit, security, and platform behavior proportional to the change. Run the full affected app suite after behavior changes.
- Update affected documentation and examples with the implementation.
- Keep each roadmap phase usable and releasable. Maintain a checkbox phase checklist; for versioned apps, update the version after each completed phase (patch for fixes, minor for features, major only when requested) and synchronize all version sources and history.
- Use focused commits with imperative subjects. PRs should cover behavior, resource impact, dependencies, security, and tests.
