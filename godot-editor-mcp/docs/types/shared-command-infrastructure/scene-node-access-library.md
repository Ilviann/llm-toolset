# Library: scene-node access

**Source:** `plugin/addons/godot_mcp/scene_node_access.gd`

Validates scene-relative node paths and node names, resolves `.` or descendants only inside the edited scene root, and returns shared success/failure envelopes. Scene node paths are a distinct vocabulary from project filesystem paths.
