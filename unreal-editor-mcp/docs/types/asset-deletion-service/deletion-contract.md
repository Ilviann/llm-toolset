# Deletion contract

`asset_delete` accepts only:

```json
{
  "operation_id": "0123456789abcdef0123456789abcdef",
  "asset_path": "/Game/Data/DA_Disposable.DA_Disposable",
  "expected_snapshot": "0123456789abcdef0123456789abcdef01234567"
}
```

The snapshot is the latest exact `asset_references.snapshot_id`. The service first revalidates that snapshot without mutation. If the exact target was unloaded, deletion may then load it; this does not authorize candidate-referencer loads. A second bounded reference snapshot must have complete serialized, management, and searchable-name scans, a supported/current live-memory scan, and zero records. Because a normal editor can contain more than the 8,192-object diagnostic ceiling, `bounded_live_scan_complete` may be false; Unreal's deletion-specific full memory/Undo reference check must then cover the complete loaded state and always report no reference.

The target must be one persisted writable asset in one package under `/Game` or a symlink-free local project-plugin mount. Maps/worlds, current maps, redirectors, external actor/object packages, generated/script/PIE/transient content, dirty packages, open asset editors, read-only files, ambiguous multi-asset packages, and unsafe editor activity reject without changing the target.

Deletion uses public reference-aware `ObjectTools` primitives without force, unchecked deletion, implicit referencer rewriting, redirector fixup, permission changes, or transaction-buffer clearing. It may unload the exact deleted package and collect garbage as part of Unreal's public persistence cleanup. Source-control behavior is delegated to the configured Unreal provider after writable-file preflight.

A verified result reports `deleted:true`, registry/storage absence, both reference snapshots, `bounded_live_scan_complete`, `engine_reference_check_complete:true`, and `undo_supported:false`. The bridge retains `committed`, `partial`, or `outcome_unknown` results. `partial` means the delete API ran but registry and storage verification disagreed. Never retry a partial or unknown outcome with a new operation ID; reconcile with `operation_status`, inspect the Asset Registry/storage after restart, and use a fresh operation only if the exact asset still exists and a new reference snapshot is safe.
