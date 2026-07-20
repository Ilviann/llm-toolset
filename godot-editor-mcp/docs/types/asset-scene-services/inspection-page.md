# Types: scene inspection records and pages

**Sources:** `edited_scene_inspector.gd`, `runtime_scene_inspector.gd`, runtime tree service

Tree records expose normalized scene-relative path, name, class, parent/depth, and bounded structural metadata; runtime records additionally expose hashed runtime identity, script/source scene, groups, process mode, and visibility. Property records expose category, exact name, Godot type, and bounded encoded value.

Pages always return explicit `scope`, stable `snapshot_id`, truncation/continuation flags, and optional cursor. Edited snapshots bind scene identity plus UndoRedo/structure/property-list state. Runtime snapshots additionally bind run, debugger session, runtime object identity, and tree generation.
