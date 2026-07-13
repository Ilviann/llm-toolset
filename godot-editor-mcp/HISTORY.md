# Godot Editor MCP history

This file records released changes. Planned work is tracked separately in
[`TODO.md`](TODO.md).

## 0.4.1 — 2026-07-13

- Split the Godot editor plugin into focused asset, scene, Project Settings,
  Input Map, shared-validation, and shared-limit modules while preserving the
  authenticated bridge protocol and command behavior.

## 0.4.0 — 2026-07-13

- Added `project_settings_get` and atomic, compare-and-swap
  `project_settings_patch` tools in `small` and `large` modes.
- Added `input_map_patch` for duplicate-free key, mouse, and joypad bindings.
- Added full-batch validation, dry runs, save rollback, reload requirements,
  and capability metadata for supported setting and input-event types.

## 0.3.2 — 2026-07-13

- Separated the static tool catalog, dispatch, stdio transport, and CLI
  composition from MCP request handling.
- Preserved the public `godot_editor_mcp.server` imports and tool behavior.
- Added end-to-end stdio tests for initialization, tool listing and calls,
  parse errors, and stdout/stderr separation.

## 0.3.1 — 2026-07-13

- Added portable macOS, Linux, and Windows setup documentation and process
  behavior.
- Made `start_editor` create a new session on POSIX and a detached process
  group on Windows.
- Kept macOS as the verified platform while documenting pending native Linux
  and Windows validation.

## 0.3.0 — 2026-07-13

- Added the large-mode-only `start_editor` tool.
- Restricted the launcher to the configured project and the absolute
  `GODOT_EXECUTABLE` path, with no model-provided arguments.
- Reported launcher configuration through `capabilities` and guarded repeated
  starts.

## 0.2.0 — 2026-07-13

- Added nested `tiny`, `small`, and `large` tool modes, defaulting to `tiny`,
  with dispatch-time enforcement.
- Added `capabilities` with MCP and bridge versions, active mode, exposed
  tools, supported bridge commands, Godot version, optional features, and
  effective limits.
- Made `scan_asset` public in `small` and `large` modes.
- Expanded `editor_state` with project and bridge identity, main scene,
  filesystem scan state and generation, and run metadata.
- Added run IDs and run-transition tracking to scene control and editor state.

## 0.1.1 — 2026-07-12

- Updated and verified the plugin for Godot 4.7 stable.
- Added Godot's generated GDScript UID file and updated compatibility
  documentation.

## 0.1.0 — 2026-07-12

- Added the dependency-free stdio MCP server and authenticated localhost Godot
  editor plugin.
- Added bounded editor-state, scene inspection, selection, property editing,
  and save/run/stop tools.
- Added asset discovery, asset information, staged imports, folder and
  whitelisted resource creation, scene creation/opening, node addition, and
  PackedScene instantiation.
- Added root confinement, traversal and symlink protections, protected-folder
  rules, request and file-size limits, and no-overwrite imports.
- Added offline unit tests and a minimal Godot plugin-validation project.
