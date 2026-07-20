# Type: runtime inspection page

**Sources:** `runtime_tree_service.gd`, `runtime_scene_inspector.gd`

Run-bound specialization of the common inspection page. Results include runtime project/run/session/probe/request identity plus tree/property records, snapshot, offset/continuation data, and bounded metadata.

The editor adapter preflights cursors before sending a request and completes/updates cursor state only after a validated probe response. Runtime object IDs are hashes scoped to the current runtime identity; they are not Godot instance IDs and must not survive a replacement object/run/session.
