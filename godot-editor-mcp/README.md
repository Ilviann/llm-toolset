# Godot Editor MCP

A small, offline MCP server for controlling an open Godot 4 editor. It has no
Python dependencies and is designed for small local models in LM Studio.

The integration has two local parts:

1. A Godot editor plugin listens only on `127.0.0.1:6505`.
2. A short-lived stdio MCP process authenticates to it with a per-project token.

No protocol data is written to the MCP process's stdout except JSON-RPC.

## Tools

| Tool | Purpose |
|---|---|
| `editor_state` | Current scene, selection, play state, and Godot version |
| `scene_tree` | Scene-relative node list, limited to 200 nodes |
| `node_info` | Editable properties for one node, limited to 64 |
| `set_property` | Change one property through Godot's undo history |
| `select_node` | Select one node in the editor |
| `scene_control` | Save, run, or stop the current scene |

Node paths are relative to the edited scene root. Use `.` for the root and, for
example, `Player/Camera2D` for a child. Vector and color property values use JSON
number arrays such as `[100, 200]` or `[1, 0.5, 0, 1]`.

This server intentionally does not edit project files or execute arbitrary code.
Pair it with `rooted-files-mcp` when the model also needs to read and write
GDScript, scenes, or project configuration.

## Install the Godot plugin

Copy the bundled `addons` folder into the Godot project:

```sh
cp -R /path/to/godot-editor-mcp/plugin/addons /path/to/game/
```

Open the project in Godot 4, then enable **Project → Project Settings → Plugins →
Godot MCP Bridge**. The plugin creates `.godot/godot_mcp_token`; it stays inside
Godot's generated-data folder and should not be committed.

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
        "/path/to/game"
      ]
    }
  }
}
```

In LM Studio, open the **Program** tab and choose **Install → Edit mcp.json**.
Keep the Godot project open with the plugin enabled while using the tools.

## Test

No package installation or network access is needed:

```sh
cd /path/to/godot-editor-mcp
python3 -m unittest discover -s tests -v
```

The Python suite tests MCP initialization, tool routing, authentication, bounded
transport behavior, and safe error handling. A manual check in Godot is still
recommended because editor plugin APIs are only available inside the editor.

The `plugin` folder is also a minimal Godot project for plugin validation:

```sh
/path/to/Godot --headless --editor --path plugin --quit-after 2
```
