# Asset deletion service

## Ownership

`FUnrealMCPAssetDeletionService` owns exact single-asset deletion preflight, mutation-scope enforcement, public Unreal deletion calls, and registry/storage verification. It depends on `FUnrealMCPAssetReferenceService` for a fresh full snapshot without creating a cursor. `UnrealMCPBridge` owns operation admission and rejects concurrent retained mutations.

## Dependency direction

The service reads Asset Registry and editor/package state, loads only the exact target when necessary, and calls public `ObjectTools::GatherObjectReferencersForDeletion`, `DeleteSingleObject`, and `CleanupAfterSuccessfulDelete`. It never calls force/unchecked deletion, changes file permissions, accepts filesystem paths, fixes redirectors, rewrites referencers, or clears the transaction buffer.

## Invariants

- Input is one exact asset object path, a caller-generated operation ID, and the latest 40-hex `asset_references` snapshot.
- Mutation is confined to `/Game` and symlink-free content mounts owned by plugins below the current project's `Plugins/` directory.
- Map, current-world, redirector, generated/external, transient, script, PIE, multi-asset, unpersisted, read-only, dirty, open-editor, referenced, truncated, stale, or unsupported targets reject before mutation.
- PIE/simulation, package saving, garbage collection, transactions, Undo/Redo, asset compilation, async loading, and another retained mutation reject.
- A target that was unloaded at inspection may be loaded exactly once after the caller snapshot is revalidated. Immediately before mutation, a second bounded scan requires complete registry categories and no live records, stale state, or unsupported state. If the live diagnostic scan reaches its 8,192-object ceiling, Unreal's deletion-specific full memory/Undo reference check remains authoritative beyond that diagnostic bound.
- Deletion is not Undoable. The retained result is `committed` only when the exact asset/package are absent from both the Asset Registry and storage; disagreement is retained as `partial` and is never retry-safe.

## Verification

Run the Python schema/release suite, normal and forced-unity Editor builds, `UnrealMCP.AssetDelete`, all `UnrealMCP` Automation Tests, and `scripts/run_headless_integration.py`. The native case covers stale and retained-memory rejection plus exact persisted deletion.
