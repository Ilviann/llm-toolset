# Level service contracts

Use the index to retrieve only the contract section relevant to the task.

## Maps, revisions, and snapshots

`level_inspect` accepts one of six exact shapes:

- `{"mode":"discover",...}` with optional mounted `package_path`, exact `asset_name`, and page size;
- `{"mode":"current"}`; or
- `{"mode":"actors","map_id":...,"expected_snapshot":...,...}`;
- `{"mode":"actor","map_id":...,"expected_snapshot":...,"actor_id":...,...}`;
- `{"mode":"component","map_id":...,"expected_snapshot":...,"actor_id":...,"component_id":...,...}`; or
- a single-use 32-hex cursor plus optional page size.

Discovery records contain `section:"map"`, exact World `map_path`, package/name/mount fields, and Asset Registry World Partition/external-actor flags. Scans stop at 2,048 candidates. Pages contain at most 100 records; up to 32 cursors live for 30 seconds and are bound to the normalized query and 40-hex snapshot.

The current record contains `section:"current_map"`, project-qualified `map_id`, exact path/package/name/mount, 40-hex `map_revision`, mounted/dirty/completeness flags, bounded dirty-package names, loaded external-package count, and live World Partition/external-actor flags. `snapshot_id` binds the current map identity and revision. Untitled/template worlds remain inspectable but report `mounted:false` and cannot be passed to `level_open`.

Actor, component, and property queries require this exact current `map_id` and `snapshot_id` as `expected_snapshot`. Their complete record and safety contracts are in [Actors, components, and reflected properties](#actors-components-and-reflected-properties).

`level_open` accepts exactly `operation_id` and `map_path`. `map_path` is an Asset Registry World object path, never a filesystem path. The operation ledger provides queued/executing/committed/rejected state, digest binding, replay, conflict rejection, timeout reconciliation, 128-record capacity, and 15-minute retention.

Opening the current exact map returns `already_current:true` without reload, even when it is dirty. Switching maps requires a completely inspected clean current map and safe editor state. Success returns `opened`, `already_current`, the exact snapshot, and a full `current_map` read-back. Unsafe state returns `busy`; missing/non-World/non-exact targets return stable argument/type/not-found errors.

## Actors, components, and reflected properties

Actor-list inspection requires:

```json
{
  "mode": "actors",
  "map_id": "40 lowercase hex characters",
  "expected_snapshot": "40 lowercase hex characters",
  "filters": {},
  "page_size": 25
}
```

`filters` is optional and accepts only exact `actor_id`, `label`, `class_path`, `tag`, `folder`, `data_layer`, Boolean `loaded`, and finite ordered `region.min`/`region.max` XYZ values. Actor identities are `map_id:actor_guid`, where the Actor GUID is 32 lowercase hexadecimal characters. This qualification prevents a GUID from being reused accidentally after a map transition.

World Partition pages come from descriptors and therefore include unloaded actors without loading them. `loaded` means a valid actor is currently registered in its level; an unregistered UObject waiting for a later garbage collection is not reported as loaded. A record has `section:"actor"`, identity/GUID, label, base and native class paths, transform, bounded tags and data layers with truncation flags, folder, attachment parent identity, loaded/descriptor flags, bounds availability/value, spatial loading/runtime grid, exact external package, and current loaded-package dirty state. Non-partitioned maps expose the equivalent fields from loaded persistent-level actors.

Exact actor inspection adds `actor_id` and optional `property_names`. It returns the actor record, all bounded component records, then requested actor properties. Exact component inspection additionally requires one 32-hex actor-scoped `component_id`; it returns the owning actor, selected component, and requested component properties. Component records include name, class, registration/activity, scene transforms when applicable, and `creation_method` as `native_default`, `blueprint_created`, `construction_script`, `instance`, or `unknown`.

Only exact actor/component modes may create a temporary World Partition reference, and only for the requested actor. If live data cannot be made available, `actor_unavailable` includes a bounded `unloaded_reason`; a missing descriptor/component is `not_found`. The reference is scoped to the request. Broad pages never load actors.

Properties are returned only when explicitly named and editable or Blueprint-visible. Each record reports a stable property identity, owner kind/identity, visibility flags, shared typed-value metadata, and recursively encoded value. Transient, deprecated, editor-only, delegate, interface, exported, instanced-object-graph, hidden, missing, and unsupported value forms fail with `unsupported_property`.

Executable limits are published by `capabilities`: 4,096 scanned actors/descriptors, 2,048 retained actor records, 64 components, 64 tags, 32 data layers, one targeted load, 32 requested properties, recursive game-data value depth four, 100 records per page, 32 retained cursors for 30 seconds, 64 KiB requests, and 256 KiB responses. `scan_truncated` distinguishes a bounded partial scan. All actor modes reject a changed map identity or snapshot as `stale_precondition`.
