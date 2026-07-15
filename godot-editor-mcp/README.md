# Godot Editor MCP

A small, offline MCP server for controlling an open Godot 4 editor. It is
verified with Godot 4.7 stable, has no Python dependencies, and is designed for
small local models in LM Studio.

See [`HISTORY.md`](HISTORY.md) for released changes and
[`ROADMAP.md`](ROADMAP.md) for planned features.

## Platform support

The Python server and Godot plugin support macOS, Linux, and Windows with Python
3.10 or newer. macOS with Godot 4.7 stable is currently verified; native Linux
and Windows validation is pending. The bridge uses only localhost TCP, Godot
APIs, and standard-library Python. `start_editor` creates a new session on
macOS/Linux and a detached process group on Windows.

The integration has two local parts:

1. A Godot editor plugin listens only on `127.0.0.1:6505`.
2. A short-lived stdio MCP process authenticates to it with a per-project token.

No protocol data is written to the MCP process's stdout except JSON-RPC.

## Python package layout

The dependency-free Python package separates stable responsibilities so changes
to one boundary do not require editing the entire server:

- `server.py` validates MCP requests and preserves the package's public server API.
- `tool_catalog.py` contains one typed specification per tool and derives schemas,
  stable mode ordering, bridge routes, path/wait policy, and checked bridge contracts.
- `schema_validation.py` enforces the published dependency-free JSON Schema
  subset before any local or bridge-backed tool call is dispatched.
- `tool_dispatch.py` resolves those specifications, applies project-path policy,
  and coordinates focused bridge, import, folder, and editor-launch handlers.
- `stdio.py` owns newline-delimited JSON-RPC input/output and stderr diagnostics.
- `cli.py` parses arguments and composes the bridge, asset manager, launcher,
  and MCP server.
- `errors.py` defines bounded error envelopes and stable typed exceptions.
- `state_payloads.py` validates the editor-state and reload-status fields used
  by waits; `waiting.py` owns monotonic deadlines, polling, cancellation,
  diagnostic quiet periods, completion predicates, and startup health windows.
- `discovery.py` validates project-scoped bridge heartbeat records and selects a
  live matching port when `--port` is omitted.
- `assets.py`, `bridge.py`, and `launcher.py` remain focused adapters for their
  filesystem, localhost transport, and process responsibilities.

All expected asset, launcher, argument-validation, bridge, timeout, and
cancellation failures inherit one structured domain-error boundary. Unexpected
Python exceptions remain internal errors instead of being presented as normal
tool failures.

This organization keeps the model-facing schemas easy to audit, makes service
dependencies replaceable in unit tests through small structural interfaces, and
retains the existing `MCPServer`, mode/catalog constants, `run`, and `main`
imports from `godot_editor_mcp.server` for compatibility.

## Godot plugin layout

The dependency-free editor plugin is split by responsibility under
`plugin/addons/godot_mcp`:

- `godot_mcp.gd` owns only plugin lifecycle and service composition.
- `token_store.gd`, `authenticated_startup.gd`, `bridge_server.gd`, and
  `command_router.gd` own fail-closed credential persistence, authenticated
  startup, bounded localhost clients and request deadlines, immediate or
  debugger-deferred dispatch, and duplicate-safe command ownership.
- `editor_state_monitor.gd` is the stable state facade over focused scene, run,
  import, and project-file trackers; `event_store.gd` and
  `operation_registry.gd` retain shared monotonic identities.
- `diagnostic_store.gd` is a thread-safe bounded Godot logger and read API.
- `project_identity.gd` and `atomic_json_record.gd` provide the shared
  cross-platform identity and bounded crash-safe record primitives used by
  discovery and reload recovery.
- `discovery_record.gd` publishes the project-scoped bridge heartbeat.
- `reload_commands.gd` safeguards scene/run state, persists bounded pending
  reloads, invokes the deferred restart, and validates startup recovery.
- `error_envelope.gd` centralizes bounded bridge success and failure envelopes.
- `asset_commands.gd` handles asset discovery, resource creation, and scene
  file creation or opening.
- `edited_scene_inspector.gd` owns bounded edited-scene reads and scope routing;
  `runtime_scene_inspector.gd` adapts runtime reads to the same cursor contract.
- `runtime_debugger_gateway.gd` and the compact `runtime_probe.gd` shell own the
  validated editor/game debugger handshake and protocol. Focused runtime tree,
  capture, input, and condition services share one confined identity/path
  context; `runtime_gameplay_commands.gd` exposes only the fixed capture,
  action-input, and condition command family.
- `scene_commands.gd` owns UndoRedo-backed node/property changes and selection.
- `project_settings_commands.gd` and `input_map_commands.gd` handle their
  respective validated, atomic project configuration operations.
- `project_path_guard.gd`, `scene_node_access.gd`, `property_value_codec.gd`,
  and `input_event_codec.gd` are narrow collaborators injected only where used.
- `command_limits.gd` is the single source for limits used by handlers and the
  `capabilities` response.

Keep command names and wire responses stable when changing these modules. Each
command service publishes its own handler mapping; the composition root retains
the service and registers that mapping explicitly. New command families should
remain focused and receive only the narrow guards, codecs, state callbacks, and
editor services they use.

The Python server and installed Godot plugin are released and deployed
together. Their versions must match exactly; wait payloads are validated as a
lockstep contract rather than interpreted through an older-plugin fallback.

## Tool modes

Choose the exposed toolset at startup with `--mode tiny|small|large`. The
default is `tiny`. Modes are nested: every tool in a smaller mode is also
available in the larger modes. Calls to tools outside the active mode are
rejected even if a client retained an older `tools/list` response.

Every `tools/call` argument object is validated against the exact schema
published by `tools/list` before dispatch. Required fields, JSON types,
enums/constants, numeric and collection bounds, object property limits,
unknown fields, nested `oneOf` shapes, local schema references, and the
`res://` path pattern are enforced without a runtime dependency. Explicit
non-object `params` or `arguments`, including falsy values such as `null`, are
rejected rather than treated as empty objects. Godot handlers retain their own
validation as a second boundary.

- **`tiny` (default):** 12 focused tools for supervised GDScript-adjacent scene
  work, including compact scene inspection, UI/node construction, property
  edits, and save/run controls. It is intended for models with context windows
  below 8k. Pair it with a confined text-file MCP when GDScript itself must be
  edited; this server never exposes arbitrary file writes.
- **`small`:** 28 tools for autonomous local agents around 16k context. It adds
  bounded asset discovery, import scanning, staged imports, folders, and
  whitelisted resource creation, plus atomic project-setting and Input Map
  editing, guarded autoload workflows, read-only editor-plugin metadata, and the
  fixed gameplay validation loop. This is the appropriate mode when the agent
  must perform its own unit-test-oriented setup and verification.
- **`large`:** all 30 tools. It adds `select_node` and the opt-in
  `start_editor` launcher for models that also control the Godot desktop UI.

Example:

```sh
python3 server.py /path/to/game --mode small
```

On Windows PowerShell, use:

```powershell
py -3 server.py "C:\path\to\game" --mode small
```

## Tools

“All” means `tiny`, `small`, and `large`. Because modes are nested, “Small+”
means `small` and `large`.

| Tool | Modes | Purpose |
|---|---|---|
| `capabilities` | All | Active mode plus MCP/bridge versions, commands, exposed tools, optional features, and limits |
| `editor_state` | All | Project, bridge, edited scene, selection, filesystem scan, and run state |
| `get_diagnostics` | All | Bounded editor, parser, and runtime diagnostics with stable cursors |
| `reload_project` | All | Safely restart the configured project and optionally wait through reconnect |
| `create_scene` | All | Create a bounded scene tree with built-in nodes, scripts, groups, and initial properties |
| `open_scene` | All | Open an existing project scene |
| `scene_tree` | All | Targeted, paginated edited or runtime nodes with root, depth, and class filters |
| `add_node` | All | Add a built-in node through Godot's undo history |
| `instantiate_scene` | All | Add a PackedScene instance through undo history |
| `node_info` | All | Paginated edited or live runtime properties with exact name/category filters |
| `set_property` | All | Change one property through Godot's undo history |
| `scene_control` | All | Save or run the scene; stop the active run by its returned run ID |
| `list_assets` | Small+ | Filtered, paginated project assets with filesystem snapshots |
| `asset_info` | Small+ | Type, category, size, import state, and dependencies |
| `scan_asset` | Small+ | Queue a Godot filesystem scan for an existing project asset |
| `import_asset` | Small+ | Copy one staged source file into the project and queue import |
| `create_folder` | Small+ | Create a project folder |
| `create_resource` | Small+ | Create a whitelisted built-in resource as text `.tres` |
| `project_settings_get` | Small+ | Read one setting or up to 100 settings under a prefix |
| `project_settings_patch` | Small+ | Atomically validate, compare, dry-run, and save up to 32 settings |
| `input_map_patch` | Small+ | Add/remove key, mouse, and joypad bindings without duplicates |
| `list_autoloads` | Small+ | List up to 64 autoloads with normalized paths and protected status |
| `autoload_patch` | Small+ | Atomically compare, add, update, or remove up to 32 autoloads |
| `list_editor_plugins` | Small+ | Read compact installed/enabled editor-plugin troubleshooting metadata |
| `scene_transaction` | Small+ | Prevalidate and apply a bounded structural/property batch as one undo step |
| `capture_game_view` | Small+ | Return the active run's bounded main viewport as MCP PNG image content |
| `send_input` | Small+ | Inject one run-scoped Input Map action with automatic bounded release |
| `wait_for_runtime_condition` | Small+ | Wait for one play, node, count, or built-in scalar property condition |
| `select_node` | Large | Select one node in the editor for coordinated desktop inspection |
| `start_editor` | Large | Start Godot for the configured project using `GODOT_EXECUTABLE` |

Node paths are relative to the selected edited or runtime scene root. Use `.` for the root and, for
example, `Player/Camera2D` for a child. Vector and color property values retain
their concise JSON number arrays such as `[100, 200]` or `[1, 0.5, 0, 1]`.
Node, resource, NodePath, transform, enum/flags, and packed-array values use the
explicit tagged forms described under Scene construction.

### Targeted inspection and pagination

`scene_tree` defaults to `tree_scope: "edited"`, root `.`, depth 3, and 50 results. Use `root` for a
known subtree, `max_depth` from 0 through 64, `class` for an exact Godot class,
and `limit` up to 200. Returned paths always remain relative to the complete
selected scene root, even for a targeted subtree. Both inspection tools return
the selected `scope` explicitly.

`node_info` defaults to edited scope and 24 properties and accepts exact `property` and
`category` filters plus a limit up to 64. Every returned property includes its
Godot category, name, type, and bounded encoded value, so a category discovered
on one page can be used directly as a follow-up filter.

`list_assets`, `scene_tree`, and `node_info` return `snapshot_id`, `truncated`,
`continuation_available`, and an opaque `cursor` when another page is
available. Repeat the same query with that cursor; changing the folder, root,
filter, depth, or limit rejects the cursor. Filesystem changes invalidate only
asset cursors, scene identity/UndoRedo/structure changes invalidate edited-tree
cursors, and node replacement or property-list changes invalidate property
cursors with `stale_cursor`. Runtime cursors additionally bind the run,
debugger session, and runtime-tree generation. Cursors expire after two minutes, at most 128 are
retained, and their 48-character IDs contain neither project-token material nor
Godot object references. A truncated result with no continuation indicates the
bounded 5,000-item tree/asset or 1,024-entry property scan ceiling was reached.

### Read-only runtime inspection

Set `tree_scope` to `"runtime"` on `scene_tree` or `node_info` to observe the
active debug run rather than the edited scene. Runtime tree records include a
hashed `runtime_id`, scene-relative path, name, class, parent, script, source
scene, up to eight public groups, process mode, visibility, and depth. Pass the
returned path and optional `runtime_id` to `node_info`; including the identity
prevents a follow-up read from silently targeting a replacement object at the
same path.

The editor plugin temporarily registers the reserved
`GodotMCPRuntimeProbe` autoload from its own addon folder. It refuses to replace
a conflicting autoload and removes only its matching registration when the
plugin stops. The bundled probe communicates solely through Godot's public
debugger channel: it opens no game-side network port, exposes no arbitrary
runtime mutation or method-call command, and never evaluates client-provided
GDScript. Without an
active `EngineDebugger`—including ordinary non-debug runs and exports—the probe
does not register a capture or process frames.

One active debugger session is supported. Runtime requests validate the current
run ID, editor debugger-session ID, project identity, probe protocol version,
supported commands, limits, and a per-process nonce. A missing probe returns
`runtime_probe_unavailable`, multiple sessions return
`ambiguous_runtime_session`, an incompatible probe returns `version_mismatch`,
and stopped/replaced runs or debugger reconnects reject stale runtime IDs and
cursors. Runtime requests retain at most 16 pending responses. Inspection and
capture use a two-second operation bound; condition waits are bounded to ten
seconds and remain inside the existing bridge response bound.

The editor bridge also retains at most 16 localhost clients. Each newly
accepted client has two seconds to authenticate and submit one complete bounded
request; excess, idle, or partial-request clients are disconnected. A deferred
runtime response continues to occupy one of those client slots and is also
subject to the runtime gateway's independent pending-request and timeout bounds.

### Gameplay validation

The three gameplay tools are available in `small` and `large` modes and require
the exact active `run_id`. `capture_game_view` reads only the running project's
main viewport, preserves its aspect ratio, and accepts output maxima up to
2,048 by 2,048 and 4,194,304 pixels. Source viewports are capped at 8,192 per
dimension and 33,554,432 pixels; staged PNG data is capped at 8 MiB and two
seconds. The runtime can stage only
`.godot/godot_mcp/captures/<capture-id>.png`. Python derives that path from the
validated 32-character identity, rejects symlinks, checks file size, PNG
signature, and IHDR dimensions, returns MCP image content, and deletes the
file. The probe also removes abandoned captures after two minutes. A headless
display server returns `unsupported_capability` because it has no game image.

`send_input` accepts only an existing Input Map action, `strength` from 0 to 1,
and an explicit `duration_ms` (up to 10,000) or `frames` (up to 600) for a
press. At most eight injected actions may be held. Release is scheduled inside
the game process, so it still occurs if the MCP connection closes; all held
actions are also released when the probe or run shuts down. `pressed:false`
releases an action immediately. Runtime output marks these records with
`GodotMCPInjectedInput` so injected actions are distinguishable from physical
input diagnostics.

`wait_for_runtime_condition` requires `scope:"runtime"` and supports exactly
four non-composable condition types: `play_state`, `node_exists`, `node_count`,
and `property`. Play state accepts `expected_state:"running"|"stopped"`.
Node existence accepts a scene-relative `path` and `exists`; node counts accept
an optional exact `group`, depth up to 64, comparison, and non-negative count.
Property conditions accept one scene-relative node, one built-in Godot scalar
property, and `eq`, `ne`, `lt`, `lte`, `gt`, or `gte`. Restricting conditions
to built-in scalar properties avoids invoking project-defined getters. Timeouts
are at most ten seconds, traversal stops after 5,000 nodes, and evidence
contains at most eight paths. There are no scripts, expressions, regexes,
method calls, signals, nested property traversal, or `all`/`any` composition.

Example observe-act-verify sequence after `scene_control` returns run ID 3:

```json
{"run_id":3,"action":"jump","strength":1,"frames":2}
```

```json
{"scope":"runtime","run_id":3,"condition":"node_exists","path":"Player/Landed","exists":true,"timeout_ms":2000}
```

Asset paths are relative to the Godot project and omit `res://`. Asset results
include `res://` so they can be used directly in Godot properties. The asset
type filter accepts `scene`, `script`, `image`, `model`, `audio`, `font`,
`material`, `resource`, or `all`. Model-facing asset and node paths always use
forward slashes, including when the MCP server runs on Windows.

The server creates folders, scenes, and staged asset copies, but it does not
execute arbitrary code or provide general filesystem access. Pair it with
`rooted-files-mcp` when the model needs to edit GDScript or project configuration.

`start_editor` is deliberately not a general process runner. It accepts no
arguments from the model and launches only `GODOT_EXECUTABLE --editor --path`
for the project configured when the MCP server starts. The executable must be
an absolute path to an executable file. If the project editor is already
connected, the tool returns `already_running`; repeated calls while a process
started by this MCP server is still launching return `starting`. The plugin
must already be installed and enabled in the project. The MCP server does not
provide a tool to close the editor.

The fixed context cost depends on the selected mode. `tiny` omits the sixteen
asset, settings, autoload, plugin-metadata, transaction, and gameplay workflow
schemas, while `small` omits the two desktop-only selection and launcher helpers.
Exact token usage varies by model and by how the MCP client represents tool
definitions. Enabling `rooted-files-mcp` alongside this server adds its own tool
definitions, so prefer `tiny` for tightly supervised GDScript and UI work.

`capabilities` is the authoritative compatibility check. After MCP
initialization, its result includes the negotiated protocol version and combines
the Python MCP server's version, active mode, and exposed MCP tool names with
the plugin's version, supported bridge commands, optional-feature flags, Godot
version, and effective limits. Runtime inspection, game-view capture, input
injection, and runtime conditions are reported with probe protocol, autoload
availability, commands, bounds, and active/ready debugger-session counts.
Targeted inspection and stable pagination are reported as supported, along
with tree depth/scan, property scan, cursor count/length, and lifetime limits.
Scene transaction support reports its value forms and operation, created-node,
tree-depth, retained-undo, nesting, element, string, packed-array, and encoded-byte
limits. Tagged node and resource reference support is reported explicitly.
Project-workflow support reports autoload operations, compare-and-swap support,
the protected runtime-probe name, exact runtime-condition types/comparisons, and
autoload, autoload-change, and editor-plugin result limits.
Diagnostics report separate GDScript, C#, and runtime capability flags because
complete C# compiler capture depends on the installed Godot build.

Bridge failures have a stable JSON envelope with `code`, `message`, bounded
`details`, and `retryable`. The Python client turns known codes into typed
exceptions and preserves all four fields in MCP tool errors. It continues to
decode old string failures so the Python package and installed plugin can be
upgraded in either order; matching versions are still recommended.

## Project settings and Input Map

The three settings tools are available in `small` and `large` modes.
`project_settings_get` reads an exact key by default; set `recursive` to `true`
to list a prefix, bounded to 100 results. Results include the current value,
Godot variant type, known default, whether the value differs, and the required
reload level.

`project_settings_patch` validates the entire batch before changing anything.
Each change can include `expected` for compare-and-swap protection. A stale
value rejects the complete transaction. `dry_run` returns the same normalized
`diff` as a subsequent real patch, and `save` defaults to `true`. Failed saves
restore all in-memory values. Settings share the bounded value codec used by
scene workflows, including finite-number checks and tagged non-scalar forms;
InputEvent records continue through their dedicated normalized event codec.
General Input Map keys must use
`input_map_patch`; editor/internal and secret-bearing keys are rejected.

```json
{
  "changes": [
    {
      "key": "display/window/stretch/mode",
      "expected": "disabled",
      "value": "canvas_items"
    }
  ],
  "save": true,
  "dry_run": false
}
```

`input_map_patch` preserves unrelated events, detects exact normalized
duplicates, and supports logical or physical keys, mouse buttons, joypad
buttons, and signed joypad axes. Device `-1` means any controller. Changes are
loaded into the live `InputMap` and saved through `ProjectSettings`, so they
remain editable in Godot's Input Map UI and persist after a full reload.

```json
{
  "action": "ui_accept",
  "deadzone": 0.5,
  "add_events": [
    {"type": "joypad_button", "button": "a", "device": -1}
  ],
  "remove_events": [],
  "save": true
}
```

Key events use `"key":"Space"` (or a numeric Godot keycode) and optional
`"physical":true` plus `shift`, `alt`, `ctrl`, and `meta`. Mouse button names
include `left`, `right`, `middle`, wheel directions, and `xbutton1/2`. Joypad
axes are `left_x`, `left_y`, `right_x`, `right_y`, `trigger_left`, and
`trigger_right`, with `direction` equal to `-1` or `1`. Each response reports
whether an editor refresh, project reload, or editor restart is required.

## Autoloads and editor plugins

The three project-workflow helpers are available in `small` and `large` modes.
`list_autoloads` returns at most 64 sorted records with name, normalized
`res://` path, singleton state, and a `protected` flag. The reserved
`GodotMCPRuntimeProbe` registration and its bundled script path cannot be
changed through these tools.

`autoload_patch` prevalidates the complete batch before using Godot's autoload
APIs. Each change has `op` (`add`, `update`, or `remove`) and `name`; add/update
also require an existing Node script or PackedScene `path`. Optional `expected`
accepts `null` for an absent entry, a path string for a singleton, or the
`{"path": ..., "singleton": ...}` object under a `list_autoloads` record's
`value` field. A stale
value rejects the full batch. Names must be valid identifiers and cannot
conflict with built-in classes, engine singletons, or global script classes.

```json
{
  "changes": [
    {
      "op": "add",
      "name": "GameState",
      "path": "res://scripts/game_state.gd",
      "expected": null
    }
  ],
  "save": true,
  "dry_run": false
}
```

Successful changes report `project_reload: true`; call `reload_project` to
activate the saved project configuration. Failed saves restore every affected
autoload. `list_editor_plugins` returns at most 64 compact installed/enabled
records from plugin configuration and project settings. It is deliberately
read-only: enabling, disabling, installing, and removing arbitrary plugins are
not exposed.

## Editor state

`editor_state` reports the project name and path, main scene, Godot and bridge
versions, bridge port, edited scene, dirty state and selection; normalized
filesystem phase/progress and generation; active/recent imports and bounded
import failures; play state, current/last run ID and diagnostic counts; the
`project.godot` content hash and reload requirement; latest event/diagnostic
IDs; and concise accepted operations. Dirty state follows the active scene's
UndoRedo history across edits, undo/redo, save, and scene changes.
The project path is absolute for issue identification; scene and asset paths
remain `res://` or project-relative.

## Diagnostics

`get_diagnostics` reads a snapshot of at most 100 records without clearing the
store or changing Godot's Output and Debugger panels. The store retains 256
records. Each has an event ID, timestamp, severity, category, bounded message,
project-relative resource location and stack frames when Godot provides them,
and the associated run ID for runtime records.

Use `scope` (`all`, `parser`, `runtime`, or `editor`), `severity` (`all`,
`error`, or `warning`), `since`, `limit`, and optional `run_id` to keep results
small. Save the returned `latest_event_id` and pass it as `since` on the next
read. A cursor older than retained history returns `stale_cursor`; reads never
delete diagnostics. GDScript parser, scene-load, editor, and engine/runtime
diagnostics are supported. `capabilities.features.csharp_diagnostics` reports
whether complete C# compiler capture is available; it is `false` in the
verified standard Godot 4.7 build.

## Bounded waits

`open_scene`, `scan_asset`, `import_asset`, `scene_control` run/stop, and
`reload_project` accept `"wait":true` and `timeout_ms` from 1 through 120000.
Waiting happens only in the Python MCP process: it polls concise editor state or
reload status with a monotonic deadline, so the Godot main loop stays
responsive. The default timeout is 10 seconds. Closing the MCP server cancels
an outstanding wait.

Wait predicates consume validated payload views. Missing or malformed state,
operation, import, run, project, or bridge identities fail as bounded protocol
errors. Install the matching Python and plugin release before using waits.

Waited run results include the new `run_id` and whether the process survived
the startup health window. Set `startup_window_ms` from 0 through 5000 on the
run action; it defaults to 250. Completed waits also allow a short diagnostic
quiet period so immediate parser, import, or startup errors reach the store.

## Safe project reload

`reload_project` is available in every mode. It accepts `stop_running`,
`save_scenes`, `wait`, and `timeout_ms`; all booleans default to `false`. An
active game blocks reload unless `stop_running` is true. Every unsaved open
scene blocks reload unless `save_scenes` is true, and the plugin verifies that
all of them were saved before scheduling the restart. There is no discard
option.

```json
{
  "stop_running": true,
  "save_scenes": true,
  "wait": true,
  "timeout_ms": 30000
}
```

Before restarting, the plugin atomically writes a bounded pending-operation
record under `.godot`, returns its operation ID, and sends the bridge response.
The restart itself is deferred until after that response. On startup, only a
fresh record for the configured project and matching bridge version is
completed. A waited call tolerates the expected disconnect, rediscovers only a
live bridge for the configured project, rereads the project token, and verifies
the project hash, bridge version, and exact operation ID before succeeding.

Malformed or stale records, a changed plugin version, a project mismatch, scene
save failure, and timeout have distinct bounded error codes. Keep the Python
package and installed plugin on the same version; deliberately changing the
plugin during a reload returns `version_mismatch` rather than reporting an
ambiguous success. Native restart and reconnect are verified on macOS with
Godot 4.7 stable. Linux and Windows use the same Godot API and Python discovery
logic, but native reload validation remains pending on those platforms.

## Agentic usage by context size

Agentic use is possible, but results such as `scene_tree`, `node_info`, and
`list_assets` can be much larger than the tool definitions. Give the model one
concrete scene-level goal, request small result sets, and save after each
completed group of mutations. For long work, start a fresh session from the
saved project state rather than carrying an ever-growing tool history.

- **Below 8k (`tiny`):** Only narrowly scoped, short sequences are realistic, such as
  checking editor state, opening a known scene, changing one property, and
  saving. Target `scene_tree` to a shallow subtree and filter `node_info` by an
  exact property or category whenever possible.
  If GDScript editing requires a file server too, keep each session narrowly
  scoped because the combined tool and result context is tight.
- **16k (`small`):** Suitable for a modest scene-building workflow involving
  assets, several nodes, property changes, and script or configuration edits
  through the file server. Work scene by scene, use targeted asset queries, and
  checkpoint with `scene_control` after each logical unit; follow cursors only
  while the next page is relevant rather than dumping whole trees.
- **Large hosted models (`large`):** Exposes the full MCP surface. Desktop-aware
  agents can use `select_node` to coordinate MCP edits with visual inspection.

## Asset imports

Imports use a separate inbox configured with `--import-root`. Both the source
and destination given to the model are relative paths. The source must remain
inside the inbox and the destination must remain inside the project.

Supported source extensions are:

```text
bmp csv exr glb gltf hdr jpeg jpg json mp3 obj ogg otf png svg ttf wav webp
```

Prefer `.glb` for 3D models because a single file can contain its textures.
A `.gltf` file whose textures or buffers are separate must have those dependency
files imported individually. Imports are limited to 100 MiB, stream-copy in
1 MiB chunks to a flushed same-directory temporary file, never overwrite
existing files, and cannot target `.godot` or `addons`. Final publication is
atomic and no-replace: a destination created after validation but before
publication remains unchanged on both POSIX and Windows. Godot scans new files
asynchronously, so `import_asset` reports
`"scan":"queued"` or `"scan":"already_running"` and returns the scan
operation ID when new work is accepted. Use `"wait":true` for a bounded result,
or observe `editor_state.active_imports`, `recent_imports`,
`filesystem_scanning`, and `filesystem_generation`, then use `asset_info` if
the model needs to confirm the final resource type. Import completion requires
the scan/reimport to end and the resource to become typed/loadable or produce a
bounded per-resource failure. Another scan is never started while Godot reports
one in progress. `scan_asset` can explicitly request a scan for an existing
project file.

Example:

```json
{
  "source": "characters/robot.glb",
  "destination": "assets/models/robot.glb",
  "wait": true,
  "timeout_ms": 30000
}
```

`create_resource` supports `StandardMaterial3D`, `ORMMaterial3D`,
`ShaderMaterial`, `Environment`, `Gradient`, `Curve`, `StyleBoxFlat`, and
`AudioStreamRandomizer`. It accepts at most 32 editable properties and saves a
text `.tres` file. Example:

```json
{
  "path": "materials/red.tres",
  "type": "StandardMaterial3D",
  "properties": {"albedo_color": [1, 0.1, 0.1, 1], "metallic": 0.4}
}
```

## Scene construction

`create_scene` accepts a built-in Godot node class for its root. Optional
`script`, `groups`, `properties`, and recursive `children` fields create a
complete bounded initial scene before any file is written:

```json
{
  "path": "scenes/player.tscn",
  "root_type": "CharacterBody2D",
  "root_name": "Player",
  "script": "scripts/player.gd",
  "groups": ["players"],
  "children": [
    {
      "type": "Camera2D",
      "name": "Camera2D",
      "properties": {"position": [0, -64]}
    }
  ]
}
```

Open that scene before changing its tree:

```json
{"path":"scenes/player.tscn"}
```

Add a built-in node with a scene-relative parent path:

```json
{"parent":".","type":"Camera2D","name":"Camera2D"}
```

Instantiate another scene in the edited scene:

```json
{"scene":"scenes/weapon.tscn","parent":".","name":"Weapon"}
```

Added nodes receive the edited scene root as owner, so Godot includes them when
the scene is saved. `add_node`, `instantiate_scene`, and `set_property` are
compact tiny-mode adapters over the same transaction engine used by
`scene_transaction`.

### Atomic scene transactions

`scene_transaction` is available in `small` and `large`. It validates the
complete batch against an isolated PackedScene snapshot before creating an
UndoRedo action. Failed validation and stale preconditions leave the edited
scene and undo history unchanged. A successful batch increments the scene's
UndoRedo version once and returns concise per-operation results, final paths,
path transitions, and final dirty state.

Node operands are explicit objects containing exactly one current `path` or
transaction-local `handle`. `add_node`, `instantiate_scene`, `rename_node`, and
`reparent_node` can bind a handle for later operations. Supported operations are
`add_node`, `instantiate_scene`, `set_property`, `remove_node`, `rename_node`,
`reparent_node`, `attach_script`, `detach_script`, `connect_signal`,
`disconnect_signal`, `add_to_group`, and `remove_from_group`.

```json
{
  "label": "Build player camera",
  "preconditions": {
    "scene": "res://scenes/player.tscn",
    "undo_version": 4
  },
  "operations": [
    {
      "op": "add_node",
      "parent": {"path": "."},
      "type": "Camera2D",
      "name": "Camera2D",
      "handle": "camera"
    },
    {
      "op": "set_property",
      "target": {"handle": "camera"},
      "property": "position",
      "value": [0, -64]
    },
    {
      "op": "add_to_group",
      "target": {"handle": "camera"},
      "group": "player_cameras"
    }
  ]
}
```

The engine permits normal locally owned nodes and safe operations on a
PackedScene instance root. It rejects inherited scenes, instantiated-scene
editable children, root removal/reparenting, ownership-changing destinations,
self-reparenting, undeclared signals/methods, and incompatible scripts or
resource classes before mutation. Remove operations retain bounded undo state;
unexpected postconditions trigger immediate undo.

Scene property values are bounded to depth 8, 256 total elements, 128 object
keys, 2,048-character strings, 4,096 packed-array elements, and 32 KiB encoded
output. Scalars and ordinary JSON arrays/dictionaries remain direct. References
are never inferred from strings:

```json
{"$type":"node","path":"Player/Camera2D"}
{"$type":"node","handle":"camera"}
{"$type":"resource","path":"res://materials/player.tres"}
{"$type":"node_path","path":"Player/Camera2D"}
```

Resource references must exist under `res://` and match the property's resource
class hint. Node references must resolve inside the edited scene and match the
property's node-class hint. Additional tagged forms cover `rect2`, `rect2i`,
`transform2d`, `plane`, `quaternion`, `aabb`, `basis`, `transform3d`, `enum`,
`flags`, and packed byte/int/float/string/vector/color arrays. All numbers must
be finite; enum and flag names must be declared by the target property.

Save explicitly with `scene_control` after a group of changes. The `run` action returns a run ID and operation ID. Pass that run ID
back when calling `stop`; stale or missing IDs are rejected. Stop returns its
own operation ID. Subsequent `editor_state` calls report whether that run is
active and why it stopped. Without `wait`, “started” means only that the editor
accepted the request. Use `wait:true` for bounded scene-open, import/scan,
run/stop, and reload completion checks.

```json
{"action":"run"}
```

If that returns `"run_id":3`, stop only that run with:

```json
{"action":"stop","run_id":3}
```

## Install the Godot plugin

Copy the bundled `addons` folder into the Godot project:

```sh
cp -R /path/to/godot-editor-mcp/plugin/addons /path/to/game/
```

Windows PowerShell equivalent:

```powershell
Copy-Item -Recurse "C:\path\to\godot-editor-mcp\plugin\addons" "C:\path\to\game\"
```

Open the project in Godot 4.7, then enable **Project → Project Settings → Plugins
→ Godot MCP Bridge**. The plugin creates `.godot/godot_mcp_token`; it stays
inside Godot's generated-data folder and should not be committed. Other Godot 4
releases may work, but 4.7 stable is the currently verified version.

Token loading and creation fail closed. If the bounded token cannot be read,
generated, written, or flushed, the plugin starts neither the localhost
listener nor its discovery heartbeat, so it never advertises a bridge that the
Python client cannot authenticate to.

While enabled, the plugin also persists its reserved runtime-probe autoload so
new debug runs can load it, and removes that exact registration on clean plugin
shutdown. A pre-existing autoload with the same name is never overwritten.

When updating, replace the installed `addons/godot_mcp` folder and restart the
plugin or editor along with the Python MCP process. Keep their versions aligned;
`capabilities` reports both versions for verification.

The plugin atomically writes `.godot/godot_mcp_bridge.json` with its process ID,
project-path hash, port, versions, and heartbeat. It never writes the token or
absolute project path there. When `--port` is omitted, the Python process uses
only a live record whose project hash matches; malformed or stale records fall
back to 6505, and another-project records are rejected.

If port 6505 is already in use, set `godot_mcp/port` to another port in the
project's `project.godot`. Automatic discovery needs no MCP argument; if you
choose an explicit override, add the same port to the MCP arguments:

```ini
[godot_mcp]
port=6506
```

```json
"args": ["/path/to/godot-editor-mcp/server.py", "/path/to/game", "--port", "6506"]
```

## Configure LM Studio

Add this entry to LM Studio's `mcp.json`, replacing all example paths. This
macOS/Linux example uses the absolute Python interpreter path:

```json
{
  "mcpServers": {
    "godot-editor": {
      "command": "/absolute/path/to/python3",
      "args": [
        "/path/to/godot-editor-mcp/server.py",
        "/path/to/game",
        "--mode",
        "small",
        "--import-root",
        "/path/to/import-inbox"
      ]
    }
  }
}
```

On Windows, use the real `python.exe` path and double each backslash in JSON:

```json
{
  "mcpServers": {
    "godot-editor": {
      "command": "C:\\Path\\To\\Python\\python.exe",
      "args": [
        "C:\\path\\to\\godot-editor-mcp\\server.py",
        "C:\\path\\to\\game",
        "--mode",
        "small",
        "--import-root",
        "C:\\path\\to\\import-inbox"
      ]
    }
  }
}
```

Find Python with `command -v python3` on macOS/Linux or
`(Get-Command python).Source` in PowerShell. The interactive `py` launcher is
convenient for commands, but LM Studio should receive the interpreter executable.

To enable the large-mode editor launcher, use `"mode", "large"` and configure
the executable in the MCP server environment. `GODOT_EXECUTABLE` must be the
absolute executable file, not an application folder. Typical values are:

- macOS: `/Applications/Godot.app/Contents/MacOS/Godot`
- Linux: `/absolute/path/to/Godot`
- Windows: `C:\\absolute\\path\\to\\Godot_v4.7-stable_win64.exe` in JSON

Complete macOS example:

```json
{
  "mcpServers": {
    "godot-editor": {
      "command": "/absolute/path/to/python3",
      "args": [
        "/path/to/godot-editor-mcp/server.py",
        "/path/to/game",
        "--mode",
        "large"
      ],
      "env": {
        "GODOT_EXECUTABLE": "/Applications/Godot.app/Contents/MacOS/Godot"
      }
    }
  }
}
```

For Linux, use the same structure with the Linux Python and Godot paths. For
Windows, use the Windows configuration above, change `small` to `large`, and add:

```json
"env": {
  "GODOT_EXECUTABLE": "C:\\absolute\\path\\to\\Godot_v4.7-stable_win64.exe"
}
```

The launcher is unavailable in `tiny` and `small` modes. In `large` mode,
`capabilities.editor_launcher.configured` reports whether the environment
variable was provided without exposing its machine-specific value.

Create the import-inbox folder before starting the MCP server. Omit the two
`--import-root` arguments if asset import is not needed; `import_asset` then
returns a clear disabled error while all other tools continue to work.
`import_asset` is exposed only in `small` and `large` mode.

For the default minimal configuration, use `"--mode", "tiny"` and omit
`--import-root`. Use `small` as shown when the model needs asset discovery,
folder/resource creation, staged imports, or explicit filesystem scans.

In LM Studio, open the **Program** tab and choose **Install → Edit mcp.json**.
Keep the Godot project open with the plugin enabled while using editor tools,
or use `start_editor` after configuring large mode as shown above.

## Test

No package installation or network access is needed:

```sh
cd /path/to/godot-editor-mcp
python3 -m unittest discover -s tests -v
```

Windows PowerShell:

```powershell
Set-Location "C:\path\to\godot-editor-mcp"
py -3 -m unittest discover -s tests -v
```

The 85-test Python suite tests MCP initialization, end-to-end stdio initialization,
tool listing and calls, per-mode dispatch, registry invariants, stable ordering,
complete routes, path/wait policy, table-driven enforcement of every published
schema keyword and nested transaction shape, executable-source release consistency,
capability contracts, authentication, bounded transport behavior, staged imports,
traversal and symlink denial, size limits, atomic publication-race and mocked
Windows no-overwrite behavior, structured and
legacy bridge errors, discovery, safe stdout/stderr error separation, typed state
and reload payloads, exact wait-version enforcement, cancellation, diagnostic
settling, stale identities, and run startup health.
A live check in Godot is still required
when claiming compatibility because editor plugin APIs are only available inside
the editor. Symbolic-link tests are skipped when the current account cannot
create links; enable Windows Developer Mode or use the required privilege to run
those security checks.

The `plugin` folder is also a minimal Godot project for plugin validation:

```sh
/path/to/Godot --headless --editor --path plugin --quit-after 2
```

Run the bounded diagnostic store checks with:

```sh
/path/to/Godot --headless --path plugin --script res://tests/phase2_diagnostics_test.gd
```

Run the reload-record and duplicate-safe router checks with:

```sh
/path/to/Godot --headless --path plugin --script res://tests/phase3_reload_record_test.gd
/path/to/Godot --headless --path plugin --script res://tests/phase4_command_router_test.gd
```

Run the focused infrastructure and state-transition checks with:

```sh
/path/to/Godot --headless --path plugin --script res://tests/phase5_infrastructure_test.gd
/path/to/Godot --headless --path plugin --script res://tests/phase6_state_trackers_test.gd
/path/to/Godot --headless --path plugin --script res://tests/phase7_cursor_store_test.gd
/path/to/Godot --headless --path plugin --script res://tests/phase8_service_boundary_test.gd
/path/to/Godot --headless --path plugin --script res://tests/phase9_runtime_inspection_test.gd
/path/to/Godot --headless --path plugin --script res://tests/phase10_gameplay_validation_test.gd
/path/to/Godot --headless --path plugin --script res://tests/phase11_scene_transaction_test.gd
/path/to/Godot --headless --path plugin --script res://tests/phase12_project_workflow_test.gd
/path/to/Godot --headless --path plugin --script res://tests/phase13_boundary_hardening_test.gd
```

On the verified macOS platform, the opt-in subprocess check validates the live
plugin capability contract, atomic scene transactions and persistence,
autoload compare-and-swap and reload persistence, editor-plugin metadata,
editor/game debugger handshake, spawned-node and
live-property reads, action injection and release, runtime conditions,
headless-renderer capture rejection, replacement-run staleness, and
authenticated reload/reconnect:

```sh
GODOT_RELOAD_INTEGRATION=1 python3 -m unittest tests.test_reload_integration -v
```

Windows PowerShell uses the same arguments:

```powershell
& "C:\path\to\Godot_v4.7-stable_win64.exe" --headless --editor --path plugin --quit-after 2
```

With Godot 4.7 stable, this check must print the bridge listening message without
GDScript or editor API errors. Headless mode also activates scenes requested by
`open_scene`, so scene mutation checks no longer need to launch Godot with the
scene path explicitly.
