# Type: editor state payload

**Sources:** `editor_state_monitor.gd`, consumed by `state_payloads.py`

Concise bridge result that merges project/Godot/bridge identity, edited scene and selection, filesystem/import state, run state, project-file hash/reload requirement, latest event/diagnostic IDs, and bounded active/recent operations.

The facade preserves stable top-level field ownership while individual trackers own transitions. Python validates only fields used by waits and tolerates extras. Renaming, changing type, or changing identity semantics of a wait-consumed field requires synchronized payload/wait and contract updates.
