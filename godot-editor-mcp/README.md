# Godot Editor MCP

A small, offline MCP server for controlling an open Godot 4 editor. It is
verified with Godot 4.7 stable, has no Python dependencies, and is designed for
small local models in LM Studio.

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
- `tool_catalog.py` contains the static tool schemas, protocol versions, and mode policy.
- `tool_dispatch.py` routes tool calls, performs project-path preflight checks,
  and coordinates imports, scans, capabilities, and editor launch.
- `stdio.py` owns newline-delimited JSON-RPC input/output and stderr diagnostics.
- `cli.py` parses arguments and composes the bridge, asset manager, launcher,
  and MCP server.
- `assets.py`, `bridge.py`, and `launcher.py` remain focused adapters for their
  filesystem, localhost transport, and process responsibilities.

This organization keeps the model-facing schemas easy to audit, makes service
dependencies replaceable in unit tests through small structural interfaces, and
retains the existing `MCPServer`, mode/catalog constants, `run`, and `main`
imports from `godot_editor_mcp.server` for compatibility.

## Tool modes

Choose the exposed toolset at startup with `--mode tiny|small|large`. The
default is `tiny`. Modes are nested: every tool in a smaller mode is also
available in the larger modes. Calls to tools outside the active mode are
rejected even if a client retained an older `tools/list` response.

- **`tiny` (default):** 10 focused tools for supervised GDScript-adjacent scene
  work, including compact scene inspection, UI/node construction, property
  edits, and save/run controls. It is intended for models with context windows
  below 8k. Pair it with a confined text-file MCP when GDScript itself must be
  edited; this server never exposes arbitrary file writes.
- **`small`:** 16 tools for autonomous local agents around 16k context. It adds
  bounded asset discovery, import scanning, staged imports, folders, and
  whitelisted resource creation. This is the appropriate mode when the agent
  must perform its own unit-test-oriented setup and verification.
- **`large`:** all 18 tools. It adds `select_node` and the opt-in
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
| `create_scene` | All | Create a scene with one built-in root node |
| `open_scene` | All | Open an existing project scene |
| `scene_tree` | All | Scene-relative node list, limited to 200 nodes |
| `add_node` | All | Add a built-in node through Godot's undo history |
| `instantiate_scene` | All | Add a PackedScene instance through undo history |
| `node_info` | All | Editable properties for one node, limited to 64 |
| `set_property` | All | Change one property through Godot's undo history |
| `scene_control` | All | Save, run, or stop the current scene |
| `list_assets` | Small+ | Filtered project assets, limited to 100 results |
| `asset_info` | Small+ | Type, category, size, import state, and dependencies |
| `scan_asset` | Small+ | Queue a Godot filesystem scan for an existing project asset |
| `import_asset` | Small+ | Copy one staged source file into the project and queue import |
| `create_folder` | Small+ | Create a project folder |
| `create_resource` | Small+ | Create a whitelisted built-in resource as text `.tres` |
| `select_node` | Large | Select one node in the editor for coordinated desktop inspection |
| `start_editor` | Large | Start Godot for the configured project using `GODOT_EXECUTABLE` |

Node paths are relative to the edited scene root. Use `.` for the root and, for
example, `Player/Camera2D` for a child. Vector and color property values use JSON
number arrays such as `[100, 200]` or `[1, 0.5, 0, 1]`.

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

The fixed context cost now depends on the selected mode. `tiny` omits the six
asset workflow schemas, while `small` omits the desktop-only selection helper.
Exact token usage varies by model and by how the MCP client represents tool
definitions. Enabling `rooted-files-mcp` alongside this server adds its own tool
definitions, so prefer `tiny` for tightly supervised GDScript and UI work.

`capabilities` is the authoritative compatibility check. Its result combines
the Python MCP server's version, active mode, and exposed MCP tool names with
the plugin's version, supported bridge commands, optional-feature flags, Godot
version, and effective limits. Optional features currently reported as
unsupported are diagnostics, runtime inspection, game-view capture, and input
injection.

`editor_state` reports the project name and path, main scene, Godot and bridge
versions, bridge port, edited scene and selection, filesystem scan status and
generation, play state, current/last run ID, last run status, and stop reason.
The project path is absolute for issue identification; scene and asset paths
remain `res://` or project-relative.

## Agentic usage by context size

Agentic use is possible, but results such as `scene_tree`, `node_info`, and
`list_assets` can be much larger than the tool definitions. Give the model one
concrete scene-level goal, request small result sets, and save after each
completed group of mutations. For long work, start a fresh session from the
saved project state rather than carrying an ever-growing tool history.

- **Below 8k (`tiny`):** Only narrowly scoped, short sequences are realistic, such as
  checking editor state, opening a known scene, changing one property, and
  saving. Avoid `scene_tree` on large scenes and `node_info` unless necessary.
  If GDScript editing requires a file server too, keep each session narrowly
  scoped because the combined tool and result context is tight.
- **16k (`small`):** Suitable for a modest scene-building workflow involving
  assets, several nodes, property changes, and script or configuration edits
  through the file server. Work scene by scene, use targeted asset queries, and
  checkpoint with `scene_control` after each logical unit; large trees and
  repeated property dumps can still exhaust the context.
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
1 MiB chunks, never overwrite existing files, and cannot target `.godot` or
`addons`. Godot scans new files asynchronously, so `import_asset` reports
`"scan":"queued"` or `"scan":"already_running"`. Use
`editor_state.filesystem_scanning` and `filesystem_generation` to observe the
scan, then use `asset_info` if the model needs to confirm the final resource
type. `scan_asset` can explicitly request a scan for an existing project file.

Example:

```json
{
  "source": "characters/robot.glb",
  "destination": "assets/models/robot.glb"
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

`create_scene` accepts a built-in Godot node class for its root:

```json
{
  "path": "scenes/player.tscn",
  "root_type": "CharacterBody2D",
  "root_name": "Player"
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
the scene is saved. `add_node`, `instantiate_scene`, and `set_property` use the
editor's undo history. Save explicitly with `scene_control` after a group of
changes. The `run` and `stop` actions return the associated run ID; subsequent
`editor_state` calls report whether that run is active and why the last run
stopped. Commands are not yet awaitable, so “started” still means the editor
accepted the request rather than that a startup health window passed.

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

When updating, replace the installed `addons/godot_mcp` folder and restart the
plugin or editor along with the Python MCP process. Keep their versions aligned;
`capabilities` reports both versions for verification.

If port 6505 is already in use, set `godot_mcp/port` to another port in the
project's `project.godot`, then add the same port to the MCP arguments:

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

The Python suite tests MCP initialization, end-to-end stdio initialization,
tool listing and calls, per-mode dispatch, capability augmentation, public scan
routing, authentication, bounded transport behavior, staged imports, traversal
and symlink denial, size limits, no-overwrite behavior, and safe stdout/stderr
error separation. A live check in Godot is still required
when claiming compatibility because editor plugin APIs are only available inside
the editor. Symbolic-link tests are skipped when the current account cannot
create links; enable Windows Developer Mode or use the required privilege to run
those security checks.

The `plugin` folder is also a minimal Godot project for plugin validation:

```sh
/path/to/Godot --headless --editor --path plugin --quit-after 2
```

Windows PowerShell uses the same arguments:

```powershell
& "C:\path\to\Godot_v4.7-stable_win64.exe" --headless --editor --path plugin --quit-after 2
```

With Godot 4.7 stable, this check must print the bridge listening message without
GDScript or editor API errors. Headless mode also activates scenes requested by
`open_scene`, so scene mutation checks no longer need to launch Godot with the
scene path explicitly.
