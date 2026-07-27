# Feature workflow

Use this workflow for every feature, fix, or refactor.

1. Start at [`index.md`](index.md). Read the owning architecture page and affected type indexes, then inspect the relevant source, tests, metadata, examples, history, roadmap, and user documentation. Expand the working set only when evidence reveals another dependency.
2. Use `draft.md`, `notice.md`, `../ROADMAP.md`, and the relevant `todo/` phase only for intended scope. Assign new responsibility to one component; trace Python/C++ data, commands, errors, limits, versions, operations, Blueprint state, and Game-thread dispatch. Validate bounded schemas on both sides.
3. Make the smallest coherent change. Add proportional normal, invalid, limit, security, and platform coverage; run focused tests, then the full suite after behavior changes. Run Unreal Automation and headless/command-line checks for editor changes and cross-process tests for bridge changes.
4. Update changed architecture/type references and their immediate indexes. Update `README.md` for user-visible changes and `HISTORY.md` or `ROADMAP.md` for release/scope changes.
5. For a release, synchronize Python and `.uplugin` metadata, runtime capabilities, tests, examples, and history.
