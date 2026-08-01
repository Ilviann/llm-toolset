# Feature workflow

Use this workflow for every feature, fix, or refactor.

1. Start at [`index.md`](index.md). Read the owning architecture page and affected type indexes, then inspect the relevant source, tests, metadata, examples, history, roadmap, and user documentation. Expand the working set only when evidence reveals another dependency.
2. Assign new responsibility to one component. Trace Python/Godot data, commands, errors, limits, versions, persistence, waits, and debugger identities. Validate bounded schemas on both sides of the bridge.
3. Make the smallest coherent change. Add proportional normal, invalid, limit, security, and platform coverage; run focused tests, then the full suite after behavior changes. Run headless editor checks for editor/bridge changes and subprocess integration tests for cross-process changes.
4. Update changed architecture/type references and their immediate indexes. Update `README.md` for user-visible changes and `HISTORY.md` or `ROADMAP.md` for release/scope changes. From the repository root, run `python internal/tools/lint_docs.py` after Markdown changes.
5. For a release, synchronize Python and plugin metadata, runtime versions, tests, examples, and history.
