# Godot Editor MCP TODO

## Priority 0: diagnostics and reliable editor state

### Add `get_diagnostics`

Return current script, scene-load, editor, and runtime diagnostics without
requiring access to the visual Output or Debugger panels.

Suggested inputs:

- `scope`: `all`, `parser`, `runtime`, or `editor`
- `severity`: `error`, `warning`, or `all`
- `since`: optional diagnostic or event cursor
- `limit`: bounded result count

Each result should include severity, category, message, resource path, source
location when available, bounded runtime stack frames, an event ID or
timestamp, and the associated run ID.

Acceptance criteria:

- MCP alone can determine whether the current project has parser or runtime
  errors.
- Historical diagnostics can be identified or excluded by cursor and run ID.
- Reading diagnostics does not clear or mutate editor panels.

### Complete `editor_state` observability

Add the remaining state:

- edited-scene dirty state
- detailed import progress and per-resource errors
- parser/runtime error and warning counts for the current run
- detection that an external `project.godot` change requires a project reload

### Make asynchronous commands awaitable

Add either a bounded `wait_for_editor_state` tool with declarative predicates,
or optional `wait` and `timeout_ms` arguments on asynchronous tools.

Predicates should cover scene activation, filesystem scan and import
completion, play-state changes, run-ID changes, and diagnostics settling after
a reload.

Acceptance criteria:

- Awaited scene opening completes only when the requested scene is active, or
  returns a structured timeout/error.
- Awaited scene runs report the new run ID and whether the process survived a
  short, configurable startup window.

### Add `reload_project`

Provide a first-class equivalent of **Project → Reload Current Project**.

Safety requirements:

- Reject reload while a scene is running unless `stop_running=true`.
- Warn that the bridge will disconnect temporarily.
- Return a reconnect token or operation ID whose completion can be reported
  after reconnection.

## Priority 1: runtime inspection and gameplay validation

### Add runtime scene inspection

Add `tree_scope: "edited" | "runtime"` to `scene_tree` and `node_info` so the
live remote tree and properties can be inspected during a run.

Runtime nodes should include path, name, class, parent, script, source scene,
groups, process mode, visibility, and a run-scoped stable identifier. Runtime
mutation should remain separate and disabled by default.

Acceptance criteria:

- Static and runtime results are clearly labeled.
- Runtime identifiers become invalid when their run ends or the run ID
  changes.

### Add `capture_game_view`

Capture the running game viewport as a PNG and return an image result or a
bounded staged-file reference. Inputs should select the viewport/window and
optionally bound output dimensions. Editor chrome should be excluded by
default.

Support the embedded-game viewport first if capture of a separate debug
process is unavailable, and return a stable unsupported-capability error for
other run modes.

### Add bounded gameplay input

Add a test-oriented `send_input` tool for a running debug scene. It should
support an action name, pressed/released state, strength, and a strictly bounded
duration or frame count, with an optional physical key event.

Safety requirements:

- Target only the debug game owned by the connected editor.
- Reject unbounded holds.
- Release injected input when a run stops or the client disconnects.
- Distinguish injected input from physical user input in diagnostics.

### Add runtime watches and assertions

Add a read-only `wait_for_runtime_condition` tool with a declarative condition
language. Conditions should cover node existence, bounded property comparisons,
signal emission, node counts, and play-state changes. Do not evaluate arbitrary
GDScript supplied by the client.

## Priority 2: scene authoring

### Add structural edit tools

Add bounded tools for:

- removing, renaming, reparenting, and duplicating nodes
- attaching and detaching scripts
- connecting and disconnecting signals
- adding and removing group membership
- setting editable children and scene ownership where appropriate

All edits should use UndoRedo, preserve valid ownership, reject invalid edits
inside inherited or instantiated scenes, and return the final node path.

### Add batched UndoRedo transactions

Allow bounded node additions, property changes, and signal connections to be
validated and committed as one atomic undo action.

Requirements:

- Validate every operation before committing.
- Roll back the complete transaction on failure.
- Bound operation count, nesting, and payload size.
- Return per-operation results and the resulting dirty state.

### Expand property conversion and resource assignment

Add safe support for:

- `Vector4`, `Vector4i`, `Rect2`, and `Rect2i`
- `Quaternion`, `Basis`, `Transform2D`, and `Transform3D`
- `AABB`, `Plane`, and packed primitive arrays
- typed arrays and dictionaries with bounded depth and size
- enum and bit-flag metadata in `node_info`
- compatible resources referenced by validated `res://` paths
- node references encoded as scene-relative `NodePath` values

Resource assignments must validate type compatibility before creating an
UndoRedo action.

### Expand `create_scene`

Add bounded options for script attachment, root groups, initial root
properties, and an initial child tree or transaction ID.

## Priority 3: project and asset workflows

### Add autoload and plugin helpers

Add scoped tools to add, update, remove, and list autoloads, and to list enabled
editor plugins.

Mutations must validate resource paths and autoload names, protect
engine-critical settings, use `ProjectSettings` rather than manual text edits,
and report whether a full project reload is required.

### Complete filesystem and import observability

Add bounded waiting, progress reporting, and per-resource import/load errors so
a caller can determine when a scanned asset is ready for use.

### Add pagination and filters

Add opaque cursors to bounded `list_assets`, `scene_tree`, and `node_info`
results. Add property-name and category filters to `node_info` so callers can
avoid full property listings.

## Priority 4: protocol quality and maintainability

The Phase 1 protocol foundation is complete: negotiated protocol reporting,
structured bridge errors, operation and event IDs, run-scoped stop requests,
focused plugin services, and project-scoped bridge discovery are implemented.
Extend those existing contracts rather than introducing parallel state or
error formats in the remaining work.

### Add focused bridge tests for planned behavior

Add coverage alongside the corresponding features for:

- property-conversion edge cases
- UndoRedo correctness and scene ownership for new mutations
- async timeouts and editor shutdown/reload behavior
- diagnostic run/cursor separation
- stale runtime-ID rejection
- pagination stability

## Suggested delivery order

1. Diagnostics, remaining editor-state fields, and awaitable commands.
2. Project reload and import-completion tracking.
3. Read-only runtime inspection and signal watches.
4. Game-view capture and bounded gameplay input.
5. Structural editing and batched UndoRedo transactions.
6. Autoload helpers, pagination, and final capability coverage.
