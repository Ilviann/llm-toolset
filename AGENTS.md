# Repository Guidelines

## Purpose and Priorities

This repository contains lightweight, offline-first MCP tools for local LLM workflows, primarily LM Studio. Support macOS, Linux, and Windows, while optimizing for small local models and a 16 GB development machine.

- Prefer the Python standard library and short-lived or stdio processes.
- Require no cloud service, account, telemetry, or runtime download.
- Minimize dependencies, startup time, memory use, tool count, and context use.
- Isolate unavoidable platform-specific behavior and test every branch.
- Pin unavoidable dependencies and document how to prepare them offline.

## Scope and Architecture

Each application lives in its own top-level folder with source, tests, README, configuration examples, `CODE.md`, and `ROADMAP.md`. Current applications are `rooted-files-mcp/` and `godot-editor-mcp/`. Keep shared documentation at the repository root; introduce shared libraries only when multiple applications need them.

Before inspecting or changing application source for a feature, read that application's `CODE.md`. Use its dependency map to select the smallest relevant working set: affected modules, their dependencies, tests, metadata, examples, history, roadmap, and documentation. Expand the set only when source evidence reveals an undocumented dependency, and update `CODE.md` when it does.

Prefer narrow responsibilities, explicit interfaces, and low coupling. Broader refactoring is appropriate when it materially improves structure, maintainability, performance, security, or testability. If useful refactoring falls outside the requested scope, recommend it with affected modules, benefits, scope, risks, and tradeoffs; do not implement it without authorization.

## MCP and Security Design

- Treat every model-supplied argument as untrusted. Validate types, lengths, paths, operations, and encoded sizes.
- Confine filesystem access to configured roots; reject traversal and symlink escapes.
- Bound recursion, collection sizes, file sizes, response sizes, active state, and execution time. Avoid loading large data entirely into memory.
- Expose a small set of distinct tools with brief descriptions, simple schemas, few arguments, concise results, and stable errors.
- Prefer configured-root-relative identifiers and paths over long absolute paths.
- Write only protocol messages to MCP stdio stdout; send diagnostics to stderr.
- Test MCP initialization, `tools/list`, and `tools/call` end to end with LM Studio-compatible framing.
- Never commit secrets, tokens, or machine-specific paths.

## Development, Testing, and Releases

Use built-in test frameworks and fast offline suites. Test normal behavior, invalid input, resource limits, security boundaries, and platform branches. Run the complete affected application suite after behavior changes.

Production code, tests, generated code, and release checks must not use prose documentation as executable input or a test fixture. Derive expected behavior and release consistency from code, package or plugin metadata, runtime contracts, and behavioral results.

Keep every roadmap phase usable and releasable: implementation, tests, documentation, and examples belong in the same phase. A roadmap that introduces feature work must maintain a synchronized top-level checklist with a checkbox, phase number, and one-line description for each phase. When the user requests cleanup after all phases are complete, retain `ROADMAP.md` with a concise statement that no feature requests remain.

After each roadmap phase, update the application's version when it is versioned: patch for fixes, minor for features, and major only on explicit request. Keep executable/package/plugin metadata, runtime-reported versions, tests, README, examples, and history consistent. Update all affected documentation before handoff.

## Godot Editor MCP Invariants

Use `godot-editor-mcp/CODE.md`, `README.md`, executable metadata, and runtime `capabilities` as the current sources for architecture, tool modes, limits, compatibility, and behavior. Keep `AGENTS.md` limited to durable invariants:

- Deploy the Python package and Godot plugin as an exact-version pair. Reject mismatches instead of adding compatibility paths.
- Keep the editor bridge authenticated, localhost-only, bounded, and fail-closed when token persistence fails. Only one bridge may own a configured port.
- Keep the runtime probe debugger-only with no game-side network listener, arbitrary mutation, arbitrary method calls, expressions, or supplied-code evaluation.
- Preserve project confinement, bounded operations, stable error envelopes, and atomic no-overwrite asset publication across POSIX and Windows.
- Imports are asynchronous and copy one file at a time; external `.gltf` dependencies must be imported separately, while `.glb` is self-contained.
- Verify editor API changes with headless plugin/bridge checks. macOS is the currently validated platform; record native Linux and Windows results when performed.
- A forced headless shutdown may emit the dummy-renderer `Parameter "t" is null` diagnostic after preview work. Treat it as a shutdown artifact only when plugin loading succeeded and no earlier script error exists.

## Contributions

Use focused commits with imperative subjects, such as `Add bounded search results`. Pull requests should describe behavior, memory/context impact, dependencies, security implications, and tests. After completing changes, propose a focused commit message.
