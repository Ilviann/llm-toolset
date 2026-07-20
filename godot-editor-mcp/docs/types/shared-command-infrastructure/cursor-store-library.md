# Library: cursor store

**Source:** `plugin/addons/godot_mcp/cursor_store.gd`

`issue` creates an opaque record; `prepare` validates syntax/kind/query before a remote snapshot is known; `resume` additionally validates the snapshot and returns the next offset. Expiry pruning and oldest-record eviction enforce bounds. Errors distinguish malformed/expired/query-mismatched cursors from stale snapshots.
