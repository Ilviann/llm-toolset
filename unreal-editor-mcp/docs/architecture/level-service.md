# Level service

## Ownership

`UnrealMCPLevelService` owns mounted World discovery, current editor-map summaries, map identities, revisions, snapshots, pagination cursors, safe map opening, and editor-delegate invalidation. It delegates actor/component/property record construction to `FUnrealMCPLevelActorInspector`. `UnrealMCPBridge` owns HTTP admission and the shared operation ledger; `level_open` is admitted as a mutation before the service runs.

## Dependency direction

The service uses the Asset Registry for bounded World discovery and exact object-path resolution. Current-map state comes from the editor world, persistent level, World Partition, loaded external-object packages, package saved hashes, and editor/core delegates. Actor inspection depends inward on the service-provided world/map/snapshot context but cannot change revisions or cursors directly. The service does not own saving or level mutation.

## Invariants

- `level_inspect` is read-only. `discover` scans at most 2,048 World assets and `actors` scans at most 4,096 descriptors or loaded actors; both sort records and return query-bound single-use cursors. `current` returns one exact map record, while exact actor/component modes require the current map identity and snapshot.
- A map identity is the project-qualified hash of its exact mounted World object path. A map revision combines persistent package state with observed dirty mutations. A current-map snapshot binds identity and revision.
- Clean World Partition streaming and external-package loading do not advance the revision. Map transitions, dirty world/package mutations, and undo/redo do.
- `level_open` accepts no filesystem path. It resolves one exact mounted World asset, returns immediately for the already-current map, and otherwise refuses PIE, simulation, save, GC, transaction, compilation, async loading, dirty state, incomplete external-package inspection, or another active mutation.
- Opening uses non-template, non-interactive `FEditorFileUtils::LoadMap` with progress UI disabled. It never saves, discards, prompts, or claims success without exact current-map read-back.

## Verification

Run the Python suite, compile adaptive and forced-unity Editor targets, run `UnrealMCP.LevelOpen` and `UnrealMCP.LevelInspect`, then run `scripts/run_headless_integration.py`. The cross-process workflow proves exact mounted discovery, real map switching, descriptor pagination, region filtering, exact actor properties, lost-response reconciliation, same-ID replay, read-back, and clean-restart snapshot/actor stability.
