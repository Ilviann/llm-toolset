# Godot MCP toolset recommendations

These recommendations are based on using the current Godot MCP bridge during the Skybound multi-scene refactor. The bridge already has a strong minimal foundation: localhost-only transport, token authentication, path validation, request limits, protected folders, whitelisted resource creation, and UndoRedo-backed scene edits.

## Implementation status

Version 0.2.0 completed the mode-aware foundation and several observability and protocol items from this roadmap:

- Added nested `tiny`, `small`, and `large` MCP toolsets, with `tiny` as the startup default and dispatch-time enforcement for hidden tools.
- Exposed `scan_asset` publicly in `small` and `large` modes.
- Added `capabilities`, including MCP/bridge versions, active mode, exposed tools, supported bridge commands, Godot version, optional-feature flags, and effective limits.
- Partially expanded `editor_state` with project name/path, main scene, bridge version/port, filesystem scan state and generation, and current/last run metadata.
- Added run IDs to `scene_control` run/stop results and transition tracking in `editor_state`.

Version 0.3.0 added a large-mode-only `start_editor` tool. It reads the fixed
Godot binary from `GODOT_EXECUTABLE`, accepts no model-provided arguments,
launches only the configured MCP project, and reports its configuration through
`capabilities`. The launcher uses a new POSIX session on macOS/Linux and a
detached process group on Windows. Editor shutdown remains intentionally outside
the toolset. Version 0.3.1 added the cross-platform launcher behavior and setup
documentation. Native Linux and Windows validation is still pending.

Version 0.3.2 added a Python organization refactor that separated the static
tool catalog, tool execution, stdio transport, and CLI composition from MCP request handling.
It preserved the public tool surface and `godot_editor_mcp.server` compatibility
imports while adding end-to-end stdio coverage for initialization, `tools/list`,
`tools/call`, parse errors, and stdout/stderr separation. This is an internal
maintainability milestone and does not change bridge behavior or tool modes.

Version 0.4.0 added `project_settings_get`, atomic compare-and-swap
`project_settings_patch`, and the higher-level `input_map_patch` in small and
large modes. The bridge now advertises supported setting/event types, validates
complete batches before mutation, supports dry runs, rolls back failed saves,
reports reload requirements, and persists duplicate-free key, mouse, and
joypad bindings through Godot's Project Settings API.

Still outstanding from Priority 0 are diagnostics, dirty/reload-pending and diagnostic counts in `editor_state`, awaitable commands, and project reload. Runtime inspection, capture/input, broader authoring, autoload helpers, pagination, structured errors, and operation IDs also remain planned. Capability flags explicitly report the unsupported runtime and diagnostic features so clients can degrade safely.

The highest-value next step is better observability. During this work, editing was straightforward through the filesystem, but confirming project reloads, distinguishing stale diagnostics from current errors, and inspecting a procedurally generated running scene required other tools.

## Priority 0: diagnostics and reliable editor state

### Add `get_diagnostics`

Return current script, scene-load, editor, and runtime diagnostics without requiring access to the visual Output or Debugger panels.

Suggested inputs:

- `scope`: `all`, `parser`, `runtime`, or `editor`
- `severity`: `error`, `warning`, or `all`
- `since`: optional diagnostic/event cursor
- `limit`: bounded result count

Suggested output per diagnostic:

- severity and category
- message
- resource path
- line and column when available
- stack frames for runtime errors
- timestamp or monotonically increasing event ID
- whether the diagnostic belongs to the current run

Why: after adding the `EventBus` autoload, the open editor temporarily displayed stale “identifier not declared” errors until the project was reloaded. `editor_state` reported only the scene and play status, so it could not distinguish old panel content from an active parser failure.

Acceptance criteria:

- A caller can determine whether the current project has parser or runtime errors using MCP alone.
- Diagnostics from an earlier run are identifiable as historical or can be excluded with a cursor/run ID.
- Reading diagnostics does not clear or mutate the editor panels.

### Expand `editor_state`

**Status:** Partially implemented in 0.2.0. Project/bridge identity, main scene,
filesystem scan/generation, and run metadata are available. Dirty state,
reload-pending detection, import detail, and diagnostic counts remain.

Add:

- project path and project name
- editor plugin/bridge version
- bridge port
- filesystem scan/import status
- edited scene dirty state
- main scene path
- current run ID
- last run exit status and stop reason
- parser/runtime error and warning counts for the current run
- whether a project reload is pending because `project.godot` changed externally

Why: `playing: false` cannot explain whether a scene was never started, failed immediately, or exited normally.

### Make asynchronous commands awaitable

`open_scene` currently returns `{"open":"requested"}` immediately, and `scene_control(run)` returns “Scene started” before proving that play state changed. Add either:

1. `wait_for_editor_state`, with predicates and a timeout; or
2. optional `wait` and `timeout_ms` arguments on asynchronous tools.

Useful predicates include:

- current scene equals a path
- filesystem scan finished
- asset import finished
- play state became true or false
- run ID changed
- diagnostics settled after a reload

Acceptance criteria:

- `open_scene(..., wait=true)` returns only after `editor_state.scene` matches the requested scene or reports a timeout/error.
- `scene_control(run, wait=true)` reports the new run ID and whether the process remained alive through a short configurable startup window.

### Add `reload_project`

Provide a first-class equivalent of **Project → Reload Current Project**, with a clear response that the bridge connection will temporarily drop and must reconnect.

Why: autoload and project-setting changes do not become reliable merely by reopening a scene. This refactor required a full project reload before `EventBus` was recognized by the already-running editor.

Suggested safety behavior:

- reject while a scene is running unless `stop_running=true`
- expose a reconnect token or operation ID
- on reconnection, report the previous reload operation as completed

## Priority 1: runtime inspection and gameplay validation

### Add runtime modes to `scene_tree` and `node_info`

Support `tree_scope: "edited" | "runtime"` and return the live remote scene tree while a scene is running.

For runtime nodes, include:

- path, name, class, and parent path
- script path
- source PackedScene path when known
- groups
- process mode
- visibility for CanvasItem/Node3D-derived nodes
- instance ID or another run-scoped stable identifier

Allow `node_info` to inspect runtime properties by path or instance ID. Keep runtime mutation separate and disabled by default.

Why: Skybound's islands, clouds, shards, and beacon are generated at runtime. The existing static `scene_tree` correctly shows only authored nodes, but it cannot verify generated island counts, beacon presence, player spawn position, or HUD state.

Acceptance criteria:

- While Stage 10 is running, MCP can count 34 runtime `StaticBody3D` islands and find exactly one beacon.
- Static and runtime results are clearly labeled and cannot be confused.
- Runtime identifiers are invalidated when the run stops or its run ID changes.

### Add `capture_game_view`

Capture the running game viewport as PNG and return an image result or a bounded staged-file reference.

Suggested inputs:

- viewport/window selector
- optional output width and height
- include editor chrome: false by default

Why: visual validation currently requires a separate computer-control tool even when Godot MCP successfully starts the scene. A game-view capture would make layout, camera, HUD, and rendering validation part of the Godot-specific workflow.

If direct capture of a separate debug process is not feasible, explicitly support the embedded-game viewport first and return a clear capability error for unsupported run modes.

### Add bounded gameplay input support

Add a test-oriented `send_input` tool for a running debug scene:

- built-in or custom action name
- pressed/released state
- strength
- duration or frame count with strict upper bounds
- optional physical key event

Why: starting the scene confirms construction but not movement, jumping, fall recovery, collection, or beacon completion. A small input API would allow deterministic smoke tests without general desktop automation.

Safety constraints:

- only target the Godot debug game belonging to the connected editor
- reject unbounded key holds
- release all injected inputs when a run stops or the client disconnects
- expose injected input separately from real user input in logs

### Add runtime watches and assertions

Provide a read-only `wait_for_runtime_condition` tool rather than arbitrary expression execution.

Useful conditions:

- node exists/does not exist
- property equals or enters a numeric range
- signal emitted, with safely encoded arguments
- node count by class, group, or path pattern
- play state changes

Example use cases:

- wait for `stage_generated`
- assert that the player returns above Y = -8 after a fall
- wait for a shard node to disappear after collection
- confirm that the result panel becomes visible after beacon entry

Keep the condition language declarative; do not evaluate arbitrary GDScript supplied by the client.

## Priority 2: scene authoring coverage

### Add common structural edit tools

The current bridge can add/instantiate nodes and set editor-visible properties, but a multi-scene refactor also commonly needs:

- `remove_node`
- `rename_node`
- `reparent_node`
- `duplicate_node`
- `attach_script` / `detach_script`
- `connect_signal` / `disconnect_signal`
- add/remove node group membership
- set editable children/owner when appropriate

All structural edits should use Godot UndoRedo, preserve scene ownership, reject edits inside inherited/instanced scenes when Godot would reject them, and return the final node path.

### Add batched UndoRedo transactions

Allow several node additions, property changes, and signal connections to be committed as one atomic undo action.

Suggested shape:

```json
{
  "label": "Create player scene",
  "operations": [
    {"op": "add_node", "parent": ".", "type": "CollisionShape3D", "name": "Collision"},
    {"op": "set_property", "path": "Collision", "property": "position", "value": [0, 0.83, 0]}
  ]
}
```

Requirements:

- validate every operation before committing
- roll back the entire transaction on failure
- cap operation count and payload size
- return per-operation results plus the resulting dirty state

Why: constructing a reusable scene through one request per node/property is slow and can leave a half-built scene after an error.

### Improve property conversion and resource assignment

Extend `_convert_value` to safely support commonly edited Godot types:

- `Vector4` / `Vector4i`
- `Rect2` / `Rect2i`
- `Quaternion`, `Basis`, `Transform2D`, and `Transform3D`
- `AABB`, `Plane`, and packed primitive arrays
- typed arrays and dictionaries with bounded depth/size
- enum and bit-flag metadata in `node_info`
- assigning compatible Resources by validated `res://` path
- assigning Node references by scene-relative NodePath where the property type permits it

For resource-valued properties, validate type compatibility before adding the UndoRedo action.

### Expand `create_scene`

Consider optional inputs for:

- script attachment
- root groups
- initial root properties
- an initial bounded child tree or a batch transaction ID

The existing one-node scene creation is safe, but it does not substantially reduce the work needed for component scenes.

## Priority 3: project and asset workflows

### Add safe project-setting and autoload tools

**Status:** Project-setting reads, atomic patches, and higher-level Input Map
editing were implemented in 0.4.0. Scoped autoload and enabled-plugin helpers
remain planned.

Provide scoped tools for:

- read project setting
- set an existing project setting
- add/update/remove an autoload
- list enabled editor plugins and autoloads

Mutating operations should:

- protect engine-critical settings unless explicitly allowed
- validate resource paths and autoload names
- report whether a full project reload is required
- preserve the existing text format through `ProjectSettings`, not manual file rewriting

Why: the EventBus refactor required editing `project.godot`, after which the open editor had to be reloaded.

### Make filesystem scanning observable

**Status:** Partially implemented in 0.2.0. `scan_asset` is public and
`editor_state` exposes scan activity plus a filesystem generation counter.
Waiting, progress, and per-resource errors remain.

Expose `scan_asset` as a public MCP tool if it is not already intended to be public, and add:

- `wait_for_scan`
- import progress/status
- per-resource import/load errors
- a filesystem generation counter

The current command queues a scan but does not let the caller know when the new scene/script is usable.

### Add pagination instead of hard truncation only

`list_assets`, `scene_tree`, and `node_info` currently cap their results. Keep the caps, but return an opaque cursor for subsequent pages. Add property-name/category filters to `node_info` so callers usually avoid requesting every property.

## Priority 4: protocol quality and maintainability

### Add a capabilities/version command

**Status:** Mostly implemented in 0.2.0. The MCP layer augments bridge
capabilities with its server version, active mode, and exposed tool names. The
negotiated MCP protocol version is available from `initialize` but is not yet
repeated in this tool's result.

Return:

- protocol version
- plugin version
- Godot version
- supported commands and optional features
- per-command limits
- runtime-inspection/capture/input availability

This allows clients to degrade gracefully instead of assuming that every installed bridge exposes the same schema.

### Return structured errors

Replace error strings alone with stable codes plus details:

```json
{
  "ok": false,
  "error": {
    "code": "SCENE_OPEN_TIMEOUT",
    "message": "Scene did not become active before the timeout",
    "details": {"requested": "res://main.tscn", "current": ""}
  }
}
```

Useful codes include unauthorized, invalid argument, protected path, not found, editor busy, import pending, no active run, stale runtime ID, timeout, and unsupported capability.

### Add operation and run IDs

**Status:** Run IDs are partially implemented in 0.2.0 for scene run/stop and
editor state. Operation IDs and run-scoped diagnostics/runtime results remain.

Asynchronous editor operations should return operation IDs. Each debug session should have a run ID included in diagnostics, runtime-node results, captures, and input requests. This prevents results from a previous run being mistaken for current state.

### Improve port-conflict reporting and discovery

When a second editor process cannot bind `127.0.0.1:6505`, report a clearer message that another editor may already own the bridge. Consider a small project-scoped discovery record containing PID/process identity, project path hash, port, protocol version, and heartbeat timestamp.

Do not weaken localhost binding or token authentication. Avoid automatically connecting a client to a bridge belonging to a different project.

### Add focused bridge tests

Recommended automated coverage:

- authentication and constant-time token comparison behavior
- malformed/oversized request handling
- path traversal and symlink rejection
- protected-folder writes
- property conversion edge cases
- UndoRedo correctness for every mutation
- scene ownership after add/reparent/duplicate operations
- async timeouts and editor shutdown/reload behavior
- diagnostic run/cursor separation
- stale runtime ID rejection
- pagination stability

## Small current-code cleanups

- [x] Remove the duplicate unreachable `return "font"` in `_asset_category`.
- [x] Expose the bridge's `scan_asset` command through a synchronized public MCP schema.
- [ ] Add protocol-version discovery to `capabilities`; bridge and MCP server version discovery is complete.
- [x] Include effective configured limits in the `capabilities` response.

## Suggested delivery order

1. `get_diagnostics`, expanded `editor_state`, run IDs, and awaitable `open_scene`/`scene_control`.
2. Project reload plus filesystem/import completion tracking.
3. Read-only runtime tree/property inspection and signal watches.
4. Game viewport capture and bounded gameplay input.
5. Structural scene-edit tools and batched UndoRedo transactions.
6. Autoload helpers, pagination, structured errors, and protocol capability discovery.

This order improves validation reliability first, keeps early additions mostly read-only, and preserves the bridge's current narrow security posture while adding the capabilities most useful for real Godot development workflows.
