# Feature documentation

This directory contains the detailed implementation, verification,
documentation, and completion requirements for current and completed roadmap
features. [`ROADMAP.md`](../../ROADMAP.md) remains the concise active feature
checklist.

## Status groups

- Planned: None.
- Active: None.
- [Completed features](completed/index.md).
- Deferred: None.

## Front matter contract

Every feature document begins with YAML front matter containing string `feature_id`, enum `status`, string-list `depends_on`, and nullable string `released_in`. The status must match its containing status directory. Runtime features omit `release_track` or set it to `runtime`; completed runtime features require a release version, while unreleased runtime features use `null`. Support-tooling features set `release_track: support-tooling` and always use `released_in: null`, including after completion. Dependencies name stable feature IDs or explicit issue IDs and must match the document's direct-prerequisite section.

## Roadmap workflow

Keep the authoritative checklist in [`ROADMAP.md`](../../ROADMAP.md)
synchronized with every unfinished runtime feature and each completed runtime
feature that is a direct prerequisite of unfinished work. Remove other completed
runtime checklist entries while retaining their feature documents below. Keep
support-tooling features in a separate **Support tooling** section after
completion.

Feature identifiers are stable names rather than execution indexes. A feature
may be implemented and completed whenever every direct dependency listed in its
description is complete. Every feature must include implementation, tests,
documentation, examples where useful, and a releasable completion gate.

macOS is the primary development host. Keep production behavior source-portable
across macOS, Linux, and Windows; isolate and unit-test platform policy branches,
and record missing applicable native verification for completed features in the
[`ROADMAP.md` native platform test backlog](../../ROADMAP.md#native-platform-test-backlog).

## Feature catalog

- [`markdown-read` — Markdown section reads and read-only Markdown host mode](completed/markdown-read.md) — Add bounded heading-section and front-matter reads plus a Markdown-only `read_text` host mode.
- [`mcp-definition-gui` — Local MCP definition generator](completed/mcp-definition-gui.md)
  — Add a tkinter helper that generates host JSON and copyable Codex STDIO
  launch fields.
- [`mcp-settings-text-preview` — Host configuration text snippets](completed/mcp-settings-text-preview.md)
  — Replace the per-field Codex preview with copyable LM Studio `mcp.json` and
  ChatGPT Codex `config.toml` snippets.

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
- For runtime work, increment the minor version after each completed feature,
  the patch version for fixes, and the major version only when explicitly
  requested. Synchronize every runtime, package, test, example, and history
  version source.
- Complete a feature only when its implementation, automated verification,
  documentation, examples where applicable, version synchronization, and
  release history satisfy its documented completion gate.

[Back to roadmap](../../ROADMAP.md)
