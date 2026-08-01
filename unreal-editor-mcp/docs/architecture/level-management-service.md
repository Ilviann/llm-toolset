# Level management service

## Ownership

`FUnrealMCPLevelManagementService` owns exact blank/template map creation, explicit current-map preconditions, the bounded World Settings allowlist, creation facts, complete loaded map-owned package saving, and reload read-back. `FUnrealMCPLevelService` remains the authority for current-map snapshots and safe opening. `FUnrealMCPAssetDeletionService` owns map-closure deletion through the existing `asset_delete` ledger operation.

## Dependency direction

The service depends on the level service for clean current-map snapshots and explicit opening, on `UWorldFactory` or Asset Tools for creation, and on the shared property codec for typed World Settings. It saves through Unreal Editor map APIs and verifies registry, storage, clean state, and requested values after reload. It never accepts `.umap` paths, changes project defaults, converts partition topology, or deletes files.

## Invariants

- Every request carries a fresh operation ID and exact current-map snapshot. Dirty, incomplete, PIE/simulation, save, GC, transaction, Undo/Redo, compile, async-load, or conflicting-operation state rejects before mutation.
- Blank creation requires explicit World Partition, streaming, and external-actor facts. Template creation inherits those facts from one exact clean mounted World and never accepts replacement creation options.
- Destinations must be new exact World object paths under `/Game` or a symlink-free local project-plugin mount. Creation never overwrites a package or mutates its template.
- Configuration targets only the exact current map. The 16-property World Settings allowlist uses the shared scalar, enum, struct, asset, and class-reference codec; `DefaultGameMode` compatibility is enforced by reflection.
- Creation/configuration returns exact changed-property read-back, effective partition/topology facts, map identity/revision/snapshot, and per-package persistence. Root maps and the bounded set of loaded external/build-data packages are all saved and independently verified. `reload_after_save` makes current-map reload verification explicit.
- Map deletion stays in `asset_delete`: it rejects current/streaming/dirty/read-only/referenced maps, enumerates at most 2,048 owned root/build-data/external packages, rejects incomplete or external references, calls public reference-aware Unreal deletion, and verifies registry plus storage absence for every package.

## Verification

Run the Python suite, adaptive and forced-unity Editor builds, `UnrealMCP.LevelManagement`, all `UnrealMCP` Automation Tests, and `scripts/run_headless_integration.py`. The focused native case covers non-partitioned and World Partition blank creation, current-map preservation, atomic rollback, duplicate and unsupported setup refusal, configure/save/reload read-back, explicit map switching, and verified inactive-map closure deletion.
