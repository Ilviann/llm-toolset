# Asset reference service

## Ownership

`FUnrealMCPAssetReferenceService` is the narrow `asset_references` facade. It validates the initial-versus-continuation request shape, delegates exact targets to the target resolver, delegates snapshot capture to the snapshot builder, and delegates page retention to the cursor store. `UnrealMCPBridge` owns HTTP admission and constructs the facade lazily; `asset_references` is read-only and never enters the operation ledger. The asset-deletion service depends only on the facade's complete in-memory snapshot contract and never creates a model-facing cursor.

## Dependency direction

The facade composes [`asset-reference-target-resolver.md`](asset-reference-target-resolver.md), [`asset-reference-registry-scanner.md`](asset-reference-registry-scanner.md), [`asset-reference-live-scanner.md`](asset-reference-live-scanner.md), [`asset-reference-snapshot-builder.md`](asset-reference-snapshot-builder.md), and [`asset-reference-cursor-store.md`](asset-reference-cursor-store.md). Dependencies flow from the facade through snapshot/page orchestration to the stateless resolvers and scanners; no lower component depends on the facade, deletion service, Blueprint components, or level mutation components.

## Invariants

- Input is one exact mounted asset object path or one retained cursor. Filesystem, traversal, transient, unresolved, and package-only targets reject.
- Registry evidence is separated into serialized package, management, and searchable-name scans. Live-memory evidence is a separate scan and is unsupported when the target is not already loaded.
- Every scan reports `complete`, `truncated`, `unsupported`, or `stale` plus candidate, scanned, and record counts.
- Results are stably sorted and bound to a 40-hex snapshot. Continuation uses query-bound, single-use cursors; any observed Asset Registry change invalidates retained cursors.
- Discovery never loads referencer packages, opens or closes editors, compiles, saves, fixes redirectors, runs garbage collection, changes selection, or mutates an asset.
- Empty complete evidence is not a proof that dynamically constructed runtime paths, external code, or weak references do not exist.

## Verification

Run the Python schema/release suite, normal and forced-unity Editor builds, `UnrealMCP.AssetReferences`, all `UnrealMCP` Automation Tests, and `scripts/run_headless_integration.py`. The native case proves registry and loaded-object evidence, pagination, cursor consumption and staleness, strict target validation, missing-target errors, and unchanged package dirtiness.
