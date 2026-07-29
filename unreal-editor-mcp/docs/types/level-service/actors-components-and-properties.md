# Actors, components, and reflected properties

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
