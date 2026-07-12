# Godot Editor MCP

A small, offline MCP server for controlling an open Godot 4 editor. It is
verified with Godot 4.7 stable, has no Python dependencies, and is designed for
small local models in LM Studio.

The integration has two local parts:

1. A Godot editor plugin listens only on `127.0.0.1:6505`.
2. A short-lived stdio MCP process authenticates to it with a per-project token.

No protocol data is written to the MCP process's stdout except JSON-RPC.

## Tools

| Tool | Purpose |
|---|---|
| `editor_state` | Current scene, selection, play state, and Godot version |
| `list_assets` | Filtered project assets, limited to 100 results |
| `asset_info` | Type, category, size, import state, and dependencies |
| `import_asset` | Copy one staged source file into the project and queue import |
| `create_folder` | Create a project folder |
| `create_resource` | Create a whitelisted built-in resource as text `.tres` |
| `create_scene` | Create a scene with one built-in root node |
| `open_scene` | Open an existing project scene |
| `scene_tree` | Scene-relative node list, limited to 200 nodes |
| `add_node` | Add a built-in node through Godot's undo history |
| `instantiate_scene` | Add a PackedScene instance through undo history |
| `node_info` | Editable properties for one node, limited to 64 |
| `set_property` | Change one property through Godot's undo history |
| `select_node` | Select one node in the editor |
| `scene_control` | Save, run, or stop the current scene |

Node paths are relative to the edited scene root. Use `.` for the root and, for
example, `Player/Camera2D` for a child. Vector and color property values use JSON
number arrays such as `[100, 200]` or `[1, 0.5, 0, 1]`.

Asset paths are relative to the Godot project and omit `res://`. Asset results
include `res://` so they can be used directly in Godot properties. The asset
type filter accepts `scene`, `script`, `image`, `model`, `audio`, `font`,
`material`, `resource`, or `all`.

The server creates folders, scenes, and staged asset copies, but it does not
execute arbitrary code or provide general filesystem access. Pair it with
`rooted-files-mcp` when the model needs to edit GDScript or project configuration.

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
`"scan":"queued"`; use `asset_info` after import if the model needs to confirm
the final resource type.

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
changes.

## Install the Godot plugin

Copy the bundled `addons` folder into the Godot project:

```sh
cp -R /path/to/godot-editor-mcp/plugin/addons /path/to/game/
```

Open the project in Godot 4.7, then enable **Project → Project Settings → Plugins
→ Godot MCP Bridge**. The plugin creates `.godot/godot_mcp_token`; it stays
inside Godot's generated-data folder and should not be committed. Other Godot 4
releases may work, but 4.7 stable is the currently verified version.

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

Add this entry to LM Studio's `mcp.json`, replacing both example paths:

```json
{
  "mcpServers": {
    "godot-editor": {
      "command": "/opt/homebrew/bin/python3",
      "args": [
        "/path/to/godot-editor-mcp/server.py",
        "/path/to/game",
        "--import-root",
        "/path/to/import-inbox"
      ]
    }
  }
}
```

Create the import-inbox folder before starting the MCP server. Omit the two
`--import-root` arguments if asset import is not needed; `import_asset` then
returns a clear disabled error while all other tools continue to work.

In LM Studio, open the **Program** tab and choose **Install → Edit mcp.json**.
Keep the Godot project open with the plugin enabled while using editor tools.

## Test

No package installation or network access is needed:

```sh
cd /path/to/godot-editor-mcp
python3 -m unittest discover -s tests -v
```

The Python suite tests MCP initialization, tool routing, authentication, bounded
transport behavior, staged imports, traversal and symlink denial, size limits,
no-overwrite behavior, and safe errors. A live check in Godot is still required
when claiming compatibility because editor plugin APIs are only available inside
the editor.

The `plugin` folder is also a minimal Godot project for plugin validation:

```sh
/path/to/Godot --headless --editor --path plugin --quit-after 2
```

With Godot 4.7 stable, this check must print the bridge listening message without
GDScript or editor API errors. Headless mode also activates scenes requested by
`open_scene`, so scene mutation checks no longer need to launch Godot with the
scene path explicitly.
