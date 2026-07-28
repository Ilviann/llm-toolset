# Asset reference cursor store

## Ownership

`FUnrealMCPAssetReferenceCursorStore` owns Asset Registry change observation, the monotonic registry serial, bounded snapshot retention, page construction, 32-hex cursor generation, expiry, single-use consumption, and stale-cursor rejection. Its source is `Private/UnrealMCPAssetReferenceCursorStore.{h,cpp}`.

## Dependency direction

The service asks the store for a capture boundary and delegates page creation/continuation. The store consumes completed snapshots but does not resolve targets or scan registry/live state.

## Invariants

- Cursors retain one exact sorted snapshot and cannot change its target.
- A cursor is single-use, expires after 30 seconds, and rejects after any observed registry add, remove, rename, update, or files-loaded event.
- Retention is limited to eight cursors and evicts the earliest-expiring entry when full.
- Page output preserves the existing response and limitations contract.

## Verification

Run the Python contract suite and `UnrealMCP.AssetReferences.RegistryLiveMemoryAndCursors`, including consumption, expiry, and registry-staleness cases.
