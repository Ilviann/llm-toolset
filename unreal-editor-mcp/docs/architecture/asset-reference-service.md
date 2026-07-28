# Asset reference service

## Ownership

`FUnrealMCPAssetReferenceService` owns exact mounted-asset resolution, inbound Asset Registry evidence, bounded live-memory evidence, deterministic reference snapshots, and reference-page cursors. `UnrealMCPBridge` owns HTTP admission and constructs the service lazily; `asset_references` is read-only and never enters the operation ledger.

## Dependency direction

The service reads public Asset Registry dependency data without loading candidate packages. It optionally reads the already-loaded target through `FindObject`, open editor state through `UAssetEditorSubsystem`, and direct strong UObject references through a bounded object iterator and `FReferenceFinder`. The latter uses reflected reference collection and `AddReferencedObjects`, not arbitrary UObject `Serialize` overrides. The service does not depend on Blueprint or level mutation components.

## Invariants

- Input is one exact mounted asset object path or one retained cursor. Filesystem, traversal, transient, unresolved, and package-only targets reject.
- Registry evidence is separated into serialized package, management, and searchable-name scans. Live-memory evidence is a separate scan and is unsupported when the target is not already loaded.
- Every scan reports `complete`, `truncated`, `unsupported`, or `stale` plus candidate, scanned, and record counts.
- Results are stably sorted and bound to a 40-hex snapshot. Continuation uses query-bound, single-use cursors; any observed Asset Registry change invalidates retained cursors.
- Discovery never loads referencer packages, opens or closes editors, compiles, saves, fixes redirectors, runs garbage collection, changes selection, or mutates an asset.
- Empty complete evidence is not a proof that dynamically constructed runtime paths, external code, or weak references do not exist.

## Verification

Run the Python schema/release suite, normal and forced-unity Editor builds, `UnrealMCP.AssetReferences`, all `UnrealMCP` Automation Tests, and `scripts/run_headless_integration.py`. The native case proves registry and loaded-object evidence, pagination, cursor consumption and staleness, strict target validation, missing-target errors, and unchanged package dirtiness.
