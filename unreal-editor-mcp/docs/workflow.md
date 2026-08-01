# Feature workflow

Use this workflow for every feature, fix, or refactor.

1. Start at [`index.md`](index.md). Read the owning architecture page and affected type indexes, then inspect the relevant source, tests, metadata, examples, history, roadmap, and user documentation. Expand the working set only when evidence reveals another dependency.
2. Use [`../ROADMAP.md`](../ROADMAP.md) and the relevant feature document under [`features/`](features/index.md) only for intended scope. Assign new responsibility to one component; trace Python/C++ data, commands, errors, limits, versions, operations, Blueprint state, and Game-thread dispatch. Validate bounded schemas on both sides.
3. Make the smallest coherent change. Add proportional normal, invalid, limit, security, and platform coverage; run focused tests, then the full suite after behavior changes. Run Unreal Automation and headless/command-line checks for editor changes and cross-process tests for bridge changes.
4. Update changed architecture/type references and their immediate indexes. Update `README.md` for user-visible changes and `HISTORY.md` or `ROADMAP.md` for release/scope changes.
5. For a release, synchronize Python and `.uplugin` metadata, runtime capabilities, tests, examples, and history.

## Implementation rules

- Use ignored `ue-test/` only as a disposable build and integration-test project; never treat its generated state as a committed contract.
- Build changes from small typed mutations. Prevalidate targets, types, limits, and stale state; use editor transactions where supported; expose bounded compile, save, and read-back results; preserve unrelated Blueprint content.
- Keep the C++ bridge localhost-only and authenticate every request with a durable, high-entropy per-project token. Fail closed on credential errors and never expose the token through discovery or heartbeat data.
- Isolate and test platform-specific discovery, paths, plugin loading, and process behavior. Validate editor changes with Unreal Automation, headless/command-line, and cross-process tests as applicable.
- Keep the `ROADMAP.md` native platform test backlog current whenever a feature is completed or native test evidence changes. Under every listed platform, include each completed feature that still lacks its applicable native verification, remove it only after that verification passes, and write `None` when the platform has no outstanding completed features.
- For destructive asset mutation, a truncated bounded live-memory diagnostic is not by itself a reference-safety failure when every registry reference category is complete and current, the diagnostic is supported and not stale, it reports no references, and Unreal's full deletion-specific memory and Undo reference check also succeeds. Reject unsupported or stale live diagnostics, truncated registry scans, any observed reference, or an incomplete full Unreal check.
