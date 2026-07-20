# Types: focused tracker records

**Sources:** `scene_state_tracker.gd`, `run_state_tracker.gd`, `import_state_tracker.gd`, `project_file_state_tracker.gd`

- Scene state: edited scene path/identity, selection, UndoRedo version, dirty/save baselines, and scene-change/open-operation state.
- Run state: play phase, current/last run IDs, accepted run/stop operations, startup/exit details, and diagnostic association.
- Import state: scan phase/progress, filesystem generation, pending/recent per-path imports, failures, and scan operations.
- Project-file state: content hash, known-write baseline, drift checks, and sticky reload requirement.

These dictionaries are merged by the facade but mutable ownership must remain disjoint. Add transition tests whenever a state field or completion rule changes.
