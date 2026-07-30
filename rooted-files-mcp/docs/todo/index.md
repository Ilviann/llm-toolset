# Roadmap feature details

This directory contains the detailed implementation, verification,
documentation, and completion requirements for current and completed roadmap
features. [`ROADMAP.md`](../../ROADMAP.md) remains the concise active feature
checklist.

## Roadmap workflow

Keep the authoritative checklist in [`ROADMAP.md`](../../ROADMAP.md)
synchronized with every unfinished feature and each completed feature that is a
direct prerequisite of unfinished work. Remove other completed checklist
entries while retaining their feature documents below.

Feature identifiers are stable names rather than execution indexes. A feature
may be implemented and completed whenever every direct dependency listed in its
description is complete. Every feature must include implementation, tests,
documentation, examples where useful, and a releasable completion gate.

macOS is the primary development host. Keep production behavior source-portable
across macOS, Linux, and Windows; isolate and unit-test platform policy branches,
and record missing applicable native verification for completed features in the
[`ROADMAP.md` native platform test backlog](../../ROADMAP.md#native-platform-test-backlog).

## Feature documents

- [`markdown-read` — Markdown section reads and read-only Markdown host mode](markdown-read.md) — Add bounded heading-section and front-matter reads plus a Markdown-only `read_text` host mode.
- [`mcp-definition-gui` — Local MCP definition generator](mcp-definition-gui.md)
  — Add a tkinter helper that generates host JSON and copyable Codex STDIO
  launch fields.

## Shared roadmap contracts

### Architecture and security

- Keep production runtime dependency-free and offline-first unless a feature
  explicitly authorizes and pins a dependency with offline preparation.
- Assign each new responsibility to one documented component. Keep source,
  architecture pages, type references, and immediate indexes synchronized.
- Treat every model-supplied value as untrusted and keep paths root-relative.
  Preserve permission checks, root confinement, traversal and symlink denial,
  requested and resolved hidden-path policy, and `.mcp` protection.
- Keep file, result, collection, recursion, schema, state, and execution limits
  explicit and bounded. Reject unsupported input rather than silently falling
  back.

### Text and MCP contracts

- Restrict text operations to fully validated UTF-8 and preserve existing BOM,
  newline, final-newline, and file-mode contracts where applicable.
- Keep the public tool surface compact. Update tool definitions, permission or
  mode filters, dispatch branches, direct-call rejection, README guidance, and
  tests together.
- Preserve LM Studio-compatible initialization, `tools/list`, and `tools/call`
  framing. Write protocol messages only to stdout and diagnostics only to
  stderr.
- Keep stable bounded errors for expected invalid, permission, path, text, and
  limit failures.

### Verification

- Add normal, invalid, limit, security, and platform coverage proportional to
  each feature. Run focused tests first and the full affected application suite
  after behavior changes.
- Unit-test POSIX and Windows policy branches. Run applicable native verification
  on macOS, Linux, and Windows when hosts are available and record missing
  completed-feature coverage in the roadmap backlog.
- Test executable behavior rather than using documentation as executable input
  or fixtures.

### Documentation and release

- Update affected architecture/type references and their immediate indexes,
  user-visible README guidance, examples, roadmap state, and history with the
  implementation.
- Increment the minor version after each completed feature, the patch version
  for fixes, and the major version only when explicitly requested. Synchronize
  every runtime, package, test, example, and history version source.
- Complete a feature only when its implementation, automated verification,
  documentation, examples where applicable, version synchronization, and
  release history satisfy its documented completion gate.

[Back to roadmap](../../ROADMAP.md)
