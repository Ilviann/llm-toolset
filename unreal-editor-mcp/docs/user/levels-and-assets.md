# Levels and assets

## Level discovery and opening

Discover mounted World assets with an optional exact package-root and asset-name filter. Results are sorted, bounded by the published scan ceiling, and continued with short-lived, query-bound, single-use cursors:

```json
{"mode":"discover","package_path":"/Game/Maps","asset_name":"Authoring","page_size":10}
```

Inspect the current editor map without mutation:

```json
{"mode":"current"}
```

The response identifies the exact map, a project-qualified map ID, revision, snapshot, dirty state, World Partition and external-actor state, and bounded dirty/external-package counts. An unsaved template map remains inspectable but is reported as unmounted.

List actors against that exact map and snapshot. World Partition lists use descriptors, so unloaded actors are visible without broad loading:

```json
{
  "mode": "actors",
  "map_id": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
  "expected_snapshot": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
  "filters": {
    "region": {
      "min": {"x": -5000, "y": -5000, "z": -1000},
      "max": {"x": 5000, "y": 5000, "z": 5000}
    },
    "tag": "Authoring",
    "loaded": false
  },
  "page_size": 25
}
```

Filters are exact for actor identity, label, class path, tag, folder, data layer, loaded state, and intersecting region. Actor identities qualify the 32-hex Actor GUID with the 40-hex map ID. Records distinguish descriptor availability from live loaded state and include transforms, bounds, attachment, external package/dirty state, and bounded tag/data-layer arrays. Continue `next_cursor` once with the normal cursor-only request.

To read components or values, select one exact actor from the page and request only supported property names:

```json
{
  "mode": "actor",
  "map_id": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
  "expected_snapshot": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
  "actor_id": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:dddddddddddddddddddddddddddddddd",
  "property_names": ["Tags"],
  "page_size": 100
}
```

Actor inspection returns stable actor-scoped component IDs and their native-default, Blueprint-created, construction-script, or instance origin. `mode:"component"` adds one returned `component_id` and can request component properties. Exact inspection may temporarily load only the selected World Partition actor; unavailable data returns `actor_unavailable`, and hidden/unsafe/unsupported properties return `unsupported_property`. The scoped reference is released before return, and inspection never saves, dirties, selects, or changes loaded regions. See [`examples/level-inspect-workflow.json`](../../examples/level-inspect-workflow.json).

Open only an exact mounted World asset path:

```json
{"operation_id":"11111111111111111111111111111111","map_path":"/Game/Maps/Authoring.Authoring"}
```

Map switching refuses PIE/simulation, save, garbage collection, transactions, asset compilation, async loading, dirty packages, or another queued mutation. It never accepts filesystem paths, saves, discards, or prompts. Reuse the operation ID with `operation_status` after a lost response. See [`examples/level-open-workflow.json`](../../examples/level-open-workflow.json) for the complete request sequence.

## Level management

`level_manage` accepts mounted `UWorld` object paths such as `/Game/Maps/Arena.Arena`; it never accepts `.umap` filenames. Start with `level_inspect {"mode":"current"}` and pass its exact `snapshot_id`. Every operation refuses dirty or incompletely inspected current-map work, PIE/simulation, saving, compilation, async loading, garbage collection, Undo/Redo, transactions, and conflicting mutations.

Create a blank map without changing the current editor map:

```json
{
  "operation_id":"0123456789abcdef0123456789abcdef",
  "operation":"create",
  "destination_path":"/Game/Maps/Arena.Arena",
  "source":{"kind":"blank"},
  "creation_options":{"world_partition":false,"world_partition_streaming":false,"external_actors":false},
  "settings":[
    {"property_name":"DefaultGameMode","value":"/Script/Engine.GameModeBase"},
    {"property_name":"KillZ","value":-25000}
  ],
  "open_after_create":false,
  "expected_current_snapshot":"0123456789abcdef0123456789abcdef01234567"
}
```

Blank creation requires all three topology choices; streaming requires World Partition, and World Partition requires external actors. Template creation instead uses `source:{"kind":"template","map_path":"/Game/Maps/Template.Template"}` and omits `creation_options`. The template must be exact and clean. Its World Partition, external-actor, streaming, and package topology are inherited and never converted. The destination must be new writable `/Game` or symlink-free local project-plugin content.

Configuration applies only to the exact current map, so open it explicitly first. It accepts at most 16 unique allowlisted World Settings fields through the shared typed property codec. Set `reload_after_save:true` to request the documented save/reload persistence proof:

```json
{
  "operation_id":"1123456789abcdef0123456789abcdef",
  "operation":"configure",
  "map_path":"/Game/Maps/Arena.Arena",
  "expected_current_snapshot":"1123456789abcdef0123456789abcdef11234567",
  "settings":[{"property_name":"WorldToMeters","value":250}],
  "reload_after_save":true
}
```

The allowlist is published in the MCP schema and includes compatible `DefaultGameMode`, world bounds/AI, gravity, scale, Kill Z, selected physics classes, color/lighting, and bounded rendering values. World Partition, external actors, streaming, collections, and arbitrary reflected fields are not post-creation edits. Results return exact changed-property read-back, effective creation facts, map identity/revision/snapshot, and per-package persistence.

Map deletion remains an `asset_delete` operation. First open another clean map, inspect `asset_references` for the inactive target, then pass that snapshot to `asset_delete`. The service enumerates at most 2,048 owned root, build-data, and external actor/object packages; refuses current/streaming/dirty/read-only/incompletely owned or referenced maps; uses Unreal's public reference-aware delete path; and verifies every package in both Asset Registry and storage views. It never removes `.umap` or sidecar files directly, never force-deletes or rewrites referencers, and does not claim Undo. Reconcile lost, partial, or unknown outcomes through `operation_status` and verify absence after restart. See [`examples/level-management-workflow.json`](../../examples/level-management-workflow.json).

## Asset references

Find inbound evidence for one exact mounted asset object path:

```json
{"asset_path":"/Game/Data/ST_WeaponStats.ST_WeaponStats","page_size":25}
```

The response separates serialized package dependencies, management dependencies, searchable-name dependencies, and loaded live-memory references. Each scan reports `complete`, `truncated`, `unsupported`, or `stale`, with candidate, scanned, and record counts. Results include referencer package/mount, dependency properties, an Asset Registry object/class when known, and direct loaded-object or open-editor evidence.

Large results use a 40-hex exact snapshot plus a short-lived, query-bound, single-use cursor. Registry changes make an outstanding cursor stale. The scan never loads referencer packages or compiles, saves, fixes redirectors, runs garbage collection, changes editors, or mutates content. Serialized evidence is package-granular, and complete empty results still cannot rule out runtime-built string paths, external code, or weak references. See [`examples/asset-references-workflow.json`](../../examples/asset-references-workflow.json).

## Asset deletion

Delete only a disposable exact asset after `asset_references` returns the latest snapshot:

```json
{
  "operation_id": "0123456789abcdef0123456789abcdef",
  "asset_path": "/Game/Data/DA_Disposable.DA_Disposable",
  "expected_snapshot": "0123456789abcdef0123456789abcdef01234567"
}
```

`asset_delete` accepts no filesystem path or force option. It is confined to `/Game` and symlink-free local project-plugin mounts. Ordinary assets must occupy one persisted writable package. World assets instead require a complete bounded closure of the root map, separate build data, and external actor/object packages; the map must not be current, loaded as a streaming level, dirty, or externally referenced. The tool also rejects redirectors, unsupported generated/external content, open target editors, read-only storage, stale/unsupported or nonempty reference scans, Undo references, unsafe editor work, and concurrent mutations. If a target was unloaded during inspection, the service may load only that exact target, then repeats preflight immediately before deletion. Registry categories must be complete; if the bounded live diagnostic reaches 8,192 objects, Unreal's deletion-specific full memory/Undo check covers the remaining loaded state and the result reports that distinction.

Deletion uses Unreal's public non-force asset/package path and is not Undoable. A result is `committed` only after both Asset Registry and storage absence are verified. A retained `partial` result means Unreal accepted deletion but verification disagreed; never retry with a new operation ID until `operation_status`, a restart, and fresh inspection establish whether the exact asset still exists. See [`examples/asset-delete-workflow.json`](../../examples/asset-delete-workflow.json).
