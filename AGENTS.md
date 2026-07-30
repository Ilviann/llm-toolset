# Repository Guidelines

## Priorities

Build lightweight, offline-first MCP tools, primarily for LM Studio, on macOS, Linux, and Windows.

- Prefer the standard library, stdio or short-lived processes, and small model-facing schemas.
- Avoid cloud services, accounts, telemetry, runtime downloads, and unnecessary dependencies.
- Bound memory, output, recursion, collection sizes, file sizes, state, and execution time.
- Isolate platform-specific behavior, test each branch, and document offline preparation for pinned dependencies.

## Structure and workflow

Applications are `rooted-files-mcp/`, `godot-editor-mcp/`, and `unreal-editor-mcp/`. Keep each app self-contained; add shared code only when multiple apps need it.

Before changing an app, follow its `AGENTS.md` and `docs/workflow.md`, beginning at `docs/index.md`. Use the indexes to locate the relevant design requirement, including a roadmap feature definition, when one exists; otherwise begin from the reported behavior, executable contract, or failing test. Follow links to identify the smallest relevant set of source, dependencies, tests, metadata, examples, roadmap/history, and documentation.

Keep one file per component under `docs/architecture/`, component-owned references under `docs/types/`, and an immediate relative-link `index.md` in every docs directory. Do not create a `CODE.md` monolith.

Executable source, project assets, configuration, build files, schemas, metadata including runtime capabilities, runtime contracts, and behavioral tests define behavior. Documentation explains them and must not be executable input or a test fixture.

Keep responsibilities narrow and interfaces explicit. Do not include unrelated refactors without authorization.

## Project knowledge

- Use project documentation to locate relevant systems, contracts, types, implementation entry points, and tests. Read only the sections required for the current task; never load entire documentation folders.
- Before editing code, identify the owning feature and architectural component; affected app, module, or plugin; process, runtime, or editor boundary; ownership or authority, lifetime, concurrency or threading, resource or asset loading, persistence, and platform constraints; and existing implementation entry points and tests.
- Search for existing symbols and implementations before introducing abstractions. Prefer established project patterns over generic framework patterns.
- Do not duplicate information that can be reliably derived from executable sources.
- Record only durable project knowledge: ownership and boundaries, invariants and contracts, non-obvious execution flows, important type semantics, architectural decisions, implementation entry points, known pitfalls, and validation procedures.
- Do not record temporary implementation details, obvious declarations, speculative conclusions, or outdated behavior.
- When documentation conflicts with implementation or another authoritative artifact, investigate and resolve the discrepancy instead of silently choosing one.
- After completing a task, update only documentation and examples affected by changed behavior, contracts, flows, types, dependencies, or validation requirements.
- Keep durable knowledge concise, searchable, linked to authoritative sources, and organized with stable IDs where reliable cross-references are useful. Follow each project's existing ID conventions.
- When durable knowledge does not fit the existing architecture, type, roadmap, or history structure, propose an additional indexed documentation set rather than forcing it into an unrelated section.

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
- Use short, meaningful alphanumeric roadmap feature IDs; `-` and `_` are allowed. Existing completed features may retain identifiers such as `phase-1` or `phase-12`.
- Treat roadmap feature IDs as stable names. List every direct prerequisite under `Depends on:` in each dependent feature description, and allow features to complete out of order once those dependencies are complete.
- Keep each roadmap feature usable and releasable. Maintain a checkbox feature checklist; for versioned apps, update the version after each completed feature (patch for fixes, minor for features, major only when requested) and synchronize all version sources and history.
- Use focused commits with imperative subjects. PRs should cover behavior, resource impact, dependencies, security, and tests.
