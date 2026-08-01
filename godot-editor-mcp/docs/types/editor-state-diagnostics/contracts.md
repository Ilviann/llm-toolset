# Editor-state and diagnostic contracts

Use the index to retrieve only the contract section relevant to the task.

## Type: editor state payload

**Sources:** `editor_state_monitor.gd`, consumed by `state_payloads.py`

Concise bridge result that merges project/Godot/bridge identity, edited scene and selection, filesystem/import state, run state, project-file hash/reload requirement, latest event/diagnostic IDs, and bounded active/recent operations.

The facade preserves stable top-level field ownership while individual trackers own transitions. Python validates only fields used by waits and tolerates extras. Renaming, changing type, or changing identity semantics of a wait-consumed field requires synchronized payload/wait and contract updates.

## Types: focused tracker records

**Sources:** `scene_state_tracker.gd`, `run_state_tracker.gd`, `import_state_tracker.gd`, `project_file_state_tracker.gd`

- Scene state: edited scene path/identity, selection, UndoRedo version, dirty/save baselines, and scene-change/open-operation state.
- Run state: play phase, current/last run IDs, accepted run/stop operations, startup/exit details, and diagnostic association.
- Import state: scan phase/progress, filesystem generation, pending/recent per-path imports, failures, and scan operations.
- Project-file state: content hash, known-write baseline, drift checks, and sticky reload requirement.

These dictionaries are merged by the facade but mutable ownership must remain disjoint. Add transition tests whenever a state field or completion rule changes.

## Type: diagnostic record

**Source:** `plugin/addons/godot_mcp/diagnostic_store.gd`

Sanitized bounded record with monotonic event ID, timestamp, severity, category/scope, message, optional project-relative resource location and bounded stack frames, plus run ID for runtime output.

Queries filter by scope, severity, `since`, limit, and optional run ID without clearing history. The store retains 256 records and returns at most 100. An observation cursor older than retained history produces `stale_cursor`. C# completeness is capability-dependent; GDScript/editor/runtime capture remains the stable core.
