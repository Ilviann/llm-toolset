# Editor state and diagnostics

## Purpose

Own independent scene, run, import/filesystem, and project-file transitions; aggregate them behind one concise state command; and retain bounded parser/editor/runtime diagnostics.

## Owned source

- `editor_state_monitor.gd` — stable aggregate facade and scene-control routing.
- `scene_state_tracker.gd` — scene identity, selection, dirty/save/change transitions.
- `run_state_tracker.gd` — run IDs, startup/stop transitions, and runtime association.
- `import_state_tracker.gd` — filesystem generations, scans, imports, and failures.
- `project_file_state_tracker.gd` — `project.godot` hash drift and reload requirement.
- `diagnostic_store.gd` — bounded thread-safe logger and diagnostic query API.

## Dependencies

Uses shared errors, events, operation IDs, and bounds. The plugin lifecycle composes and polls trackers. Scene/asset/project services receive narrow callbacks. Python waits consume the aggregate state contract.

## Invariants

- Each tracker owns a disjoint mutable state family.
- The facade merges concise records without becoming another temporal owner.
- Dirty state follows the active scene's UndoRedo history and saved baseline.
- Run/stop actions are scoped to returned run and operation IDs.
- Import completion requires a completed scan/reimport plus a typed loadable resource or bounded failure.
- `project.godot` drift sets a sticky reload requirement until known writes/reload reset it.
- Diagnostics are sanitized, bounded, non-destructive to read, and runtime records retain run association.

## Change and verification guide

State field changes require synchronized Python payload/wait updates. Keep temporal logic in the owning tracker and add transition tests. Run `tests.test_state_payloads`, `tests.test_waiting`, Phase 2 diagnostics, Phase 6 trackers, relevant server tests, and integration when debugger/run behavior changes.
