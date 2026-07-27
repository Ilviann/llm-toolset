# `asset-references` — Find asset references

**Outcome:** Agents can determine which mounted packages and live editor objects refer to one exact asset, within explicit completeness and response bounds, before proposing a destructive operation.

### Implementation

- Add a read-only `asset_references` tool accepting one exact mounted asset object path. Reject filesystem paths, traversal, unresolved assets, transient objects, and ambiguous package-only input.
- Reuse the existing bounded Asset Registry dependency primitives to enumerate inbound package referencers without loading candidate packages. Report exact referencer package and primary-asset paths, asset class when known, mount, and the dependency category and properties Unreal exposes reliably.
- Separately report bounded live-memory referencers that can prevent unloading or deletion, including open asset editors and loaded editor/world objects, without serializing arbitrary UObject graphs.
- Distinguish serialized, management/searchable-name, and live-memory evidence. Report whether each scan is complete, truncated, unsupported, or stale; absence of reported references must never imply that runtime-constructed string paths or external code references cannot exist.
- Sort and paginate results with query-bound single-use cursors and an exact reference snapshot. Bound registry candidates, live objects, records, traversal depth, strings, cursor state, Game-thread work, and response bytes.
- Preserve package dirtiness, editor selection, open editors, transactions, load state, and Asset Registry state. Reference discovery must not load referencer packages or trigger compilation, saving, garbage collection, redirector fixup, or mutation.
- Add compiled public-API probes for Unreal Engine 5.8 Asset Registry referencer queries, dependency categories/properties, asset-editor state, and bounded live-reference inspection.

### Verification

- Test hard and soft package references, management and searchable-name references where supported, map and Blueprint references, redirectors, engine and plugin mounts, open editors, loaded-world references, unreferenced assets, missing assets, and assets sharing a package.
- Test pagination, deterministic ordering, cursor expiry and staleness, scan and response truncation, unsupported dependency evidence, concurrent Asset Registry changes, and every published bound.
- Prove inspection does not load candidate referencers or alter dirty state, open editors, selection, transactions, packages, or files.
- Run Python schema tests, focused native Automation, the full affected suite, and production-bridge cross-process tests on applicable platforms.

### Documentation and completion gate

- Document reference categories, completeness fields, snapshots, pagination, limits, and unavoidable false-negative cases such as dynamically constructed runtime paths.
- Add an example that finds serialized and live-memory referencers, continues a bounded page, and explains why a complete empty result is evidence rather than an absolute runtime guarantee.
- Complete the feature only when an agent can identify actionable referencers for arbitrary visible mounted asset types without loading or mutating referencer packages.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
