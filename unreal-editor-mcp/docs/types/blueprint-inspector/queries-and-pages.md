# Queries, records, and pages

`blueprint_inspect` accepts exactly one of these argument families:

- Discovery: `mode: "discover"`, optional normalized mounted-content `package_path`, optional exact `asset_name`, and optional `page_size`.
- Inspection: `mode: "inspect"`, one exact mounted-content `asset_path`, optional `sections`, optional exact 32-character `graph_id`, optional `include_inherited`, and optional `page_size`.
- Continuation: one 32-character opaque `cursor` and optional `page_size`.

Omitting `package_path` searches all Asset Registry content mounts visible to the project. Supplying `/Game`, `/Engine`, `/PluginMount`, or a deeper package path narrows the scan. Inspection accepts package or object paths such as `/Game/Folder/Asset`, `/Engine/Folder/Asset.Asset`, or `/PluginMount/Folder/Asset.Asset` and normalizes to the object path. Raw filesystem paths, traversal, and backslashes reject. Defaults are `summary`, `parent_class`, `compile_state`, `components`, `variables`, and `graphs`. Explicit sections may additionally select `nodes`, `pins`, and `connections`.

Every success contains `mode`, a 40-character `snapshot_id`, `records`, `record_count`, `page_offset`, `scan_truncated`, and `has_more`. A partial page also contains `next_cursor` and `cursor_expires_in_ms`. Records carry a `section` discriminator: `asset`, `summary`, `parent_class`, `compile_state`, `component`, `variable`, `graph`, `node`, `pin`, or `connection`.

Discovery records are sorted by exact object path. The scan stops after 2,048 Blueprint registry candidates and reports `scan_truncated`; it does not load those assets. Deep inspection rejects more than 4,096 structural fingerprint records. Pages default to 25 and accept at most 100 records.

A cursor is single-use, expires after 30 seconds, and occupies one of 32 retained slots. It stores the normalized initial query, structural snapshot, and next offset. Continuation recomputes the query and returns `stale_precondition` if the snapshot changed. An expired, consumed, evicted, or unknown cursor returns `cursor_expired`.
