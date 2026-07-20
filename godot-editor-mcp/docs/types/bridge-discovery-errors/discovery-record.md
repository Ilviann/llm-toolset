# Type: `DiscoveryRecord`

**Sources:** `godot_editor_mcp/discovery.py`, mirrored by `plugin/addons/godot_mcp/discovery_record.gd`

Immutable validated view of `.godot/godot_mcp_bridge.json`.

The record identifies its schema/bridge version, normalized project hash, editor process, localhost port, and heartbeat time. It never contains the auth token or absolute project path. Reads bound file size and field types/ranges. Freshness and process/project ownership determine whether the discovered port can be used or a record may be removed.

Any field/version change is a cross-language persistent-schema change and requires bridge discovery tests plus live reconnect verification.
