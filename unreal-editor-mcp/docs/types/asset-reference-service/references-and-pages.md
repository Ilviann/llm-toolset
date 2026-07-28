# References and pages

`asset_references` accepts exactly one of:

- `{"asset_path":"/Game/Data/ST_WeaponStats.ST_WeaponStats","page_size":25}`; or
- `{"cursor":"0123456789abcdef0123456789abcdef","page_size":25}`.

The initial target must be an exact mounted object path. A package name, filesystem path, subobject path, traversal segment, unresolved asset, or transient loaded object rejects. Continuation cannot change the target.

Each result repeats `asset_path`, a 40-hex `snapshot_id`, target metadata, per-category `scans`, the total `record_count`, page offset, `records`, and `has_more`. A nonterminal page adds a single-use 32-hex `next_cursor` valid for 30 seconds. The cursor retains the exact sorted record snapshot; consuming, expiring, or observing any Asset Registry add/remove/rename/update/files-loaded event invalidates it.

Registry records use `section:"registry"` and distinguish `serialized`, `management`, and `searchable_name` evidence. They report the dependency category and properties, exact referencer package and mount, identifier, primary-asset ID when available, and referencer asset path/class when one Asset Registry asset can be identified. Serialized dependency granularity is package-level, so an asset in a multi-asset package is evidence for that package rather than proof of the precise exporting object.

Live records use `section:"live_memory"` and identify an `asset_editor`, `loaded_object`, or `world_object`. Direct strong-reference records include the loaded object/class/package and bounded property names. The scan does not report weak pointers and is `unsupported` when the target is not already loaded.

Every `serialized`, `management`, `searchable_name`, and `live_memory` scan reports `status`, Boolean `complete`/`truncated`/`unsupported`/`stale`, and candidate/scanned/record counts. Limits are 4,096 registry candidates, 8,192 loaded objects, 2,048 total records, 64 Asset Registry assets expanded per package, 16 live property names, traversal depth one, eight retained reference cursors, 100 records per page, 30 seconds per cursor, and the global 256 KiB response ceiling.

The `limitations` object explicitly excludes runtime-constructed string paths, external code references, and weak live references. Therefore a complete empty result is useful bounded evidence, not an absolute guarantee that runtime code can never reach the asset.

Internally, the facade preserves this wire contract while delegating exact resolution, the three registry categories, live-memory evidence, snapshot construction, and cursor retention to separate components. The deletion service consumes the same snapshot record through the facade and does not depend on any scanner or cursor implementation.
