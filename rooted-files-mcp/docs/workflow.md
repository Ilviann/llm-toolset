# Feature workflow

Use this workflow for every feature, fix, or refactor.

1. Start at [`index.md`](index.md). Read the owning architecture page, affected type indexes, and relevant [`features/`](features/index.md) feature document, then inspect the source, tests, metadata, examples, roadmap/history, and user documentation. Expand the working set only when evidence reveals another dependency.
2. Assign new responsibility to one component. Trace permissions, root confinement, hidden/protected paths, text classification, limits, and atomic revalidation. Keep paths root-relative and operations bounded.
3. Make the smallest coherent change. Add proportional normal, invalid, limit, security, and platform coverage; run focused tests, then the full suite after behavior changes.
4. Update changed architecture/type references and their immediate indexes. Update `README.md` for user-visible changes and `ROADMAP.md` or history for scope/release changes.
5. For a release, synchronize the version in Python/package metadata, initialization output, tests, examples, and history.
