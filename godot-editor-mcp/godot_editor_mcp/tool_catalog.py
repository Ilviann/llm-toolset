"""Static MCP tool definitions and mode policy.

Keeping schemas in a data-only module makes the model-facing API easy to review
without mixing it with transport or execution code.
"""

from __future__ import annotations

from typing import Literal


LATEST_PROTOCOL = "2025-11-25"
SUPPORTED_PROTOCOLS = frozenset(
    {LATEST_PROTOCOL, "2025-06-18", "2025-03-26", "2024-11-05"}
)

PATH_PROPERTY = {
    "path": {"type": "string", "description": "Scene-relative node path; . is root"}
}
RESOURCE_PATH = {"type": "string", "description": "Project-relative path without res://"}

Mode = Literal["tiny", "small", "large"]
MODES: tuple[Mode, ...] = ("tiny", "small", "large")

TOOLS = [
    {
        "name": "capabilities",
        "description": "Get bridge versions, commands, features, and limits.",
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    },
    {
        "name": "editor_state",
        "description": "Get Godot version, current scene, selection, and play state.",
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    },
    {
        "name": "list_assets",
        "description": "List project assets, limited to 100 results.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "folder": {**RESOURCE_PATH, "default": "."},
                "type": {
                    "type": "string",
                    "enum": [
                        "all", "scene", "script", "image", "model", "audio",
                        "font", "material", "resource",
                    ],
                    "default": "all",
                },
                "limit": {"type": "integer", "minimum": 1, "maximum": 100, "default": 50},
            },
            "additionalProperties": False,
        },
    },
    {
        "name": "asset_info",
        "description": "Get one asset's type, size, import state, and dependencies.",
        "inputSchema": {
            "type": "object", "properties": {"path": RESOURCE_PATH},
            "required": ["path"], "additionalProperties": False,
        },
    },
    {
        "name": "scan_asset",
        "description": "Queue a Godot filesystem scan for one project asset.",
        "inputSchema": {
            "type": "object", "properties": {"path": RESOURCE_PATH},
            "required": ["path"], "additionalProperties": False,
        },
    },
    {
        "name": "import_asset",
        "description": "Copy one staged file into the project and queue Godot import.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "source": {"type": "string", "description": "Path relative to configured import root"},
                "destination": RESOURCE_PATH,
            },
            "required": ["source", "destination"], "additionalProperties": False,
        },
    },
    {
        "name": "create_folder",
        "description": "Create a folder inside the Godot project.",
        "inputSchema": {
            "type": "object", "properties": {"path": RESOURCE_PATH},
            "required": ["path"], "additionalProperties": False,
        },
    },
    {
        "name": "create_resource",
        "description": "Create a whitelisted built-in resource as a text .tres file.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "path": RESOURCE_PATH,
                "type": {
                    "type": "string",
                    "enum": [
                        "StandardMaterial3D", "ORMMaterial3D", "ShaderMaterial",
                        "Environment", "Gradient", "Curve", "StyleBoxFlat",
                        "AudioStreamRandomizer",
                    ],
                },
                "properties": {"type": "object", "default": {}},
            },
            "required": ["path", "type"], "additionalProperties": False,
        },
    },
    {
        "name": "create_scene",
        "description": "Create a scene with one built-in root node.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "path": RESOURCE_PATH,
                "root_type": {"type": "string", "description": "Built-in Node class, e.g. Node2D"},
                "root_name": {"type": "string"},
            },
            "required": ["path", "root_type", "root_name"], "additionalProperties": False,
        },
    },
    {
        "name": "open_scene",
        "description": "Open a project scene in the Godot editor.",
        "inputSchema": {
            "type": "object", "properties": {"path": RESOURCE_PATH},
            "required": ["path"], "additionalProperties": False,
        },
    },
    {
        "name": "scene_tree",
        "description": "List the edited scene tree, limited to 200 nodes.",
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    },
    {
        "name": "add_node",
        "description": "Add a built-in node to the edited scene through undo history.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "parent": {"type": "string", "description": "Scene-relative node path; . is root"},
                "type": {"type": "string", "description": "Built-in Node class, e.g. Sprite2D"},
                "name": {"type": "string"},
            },
            "required": ["parent", "type", "name"], "additionalProperties": False,
        },
    },
    {
        "name": "instantiate_scene",
        "description": "Instantiate a PackedScene under a node through undo history.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "scene": RESOURCE_PATH,
                "parent": {"type": "string", "description": "Scene-relative node path; . is root"},
                "name": {"type": "string"},
            },
            "required": ["scene", "parent", "name"], "additionalProperties": False,
        },
    },
    {
        "name": "node_info",
        "description": "Get editable properties of one scene node.",
        "inputSchema": {
            "type": "object", "properties": PATH_PROPERTY, "required": ["path"],
            "additionalProperties": False,
        },
    },
    {
        "name": "set_property",
        "description": "Set one node property through Godot undo history.",
        "inputSchema": {
            "type": "object",
            "properties": {
                **PATH_PROPERTY,
                "property": {"type": "string"},
                "value": {"description": "JSON value; vectors and colors use number arrays"},
            },
            "required": ["path", "property", "value"],
            "additionalProperties": False,
        },
    },
    {
        "name": "select_node",
        "description": "Select one node in the Godot editor.",
        "inputSchema": {
            "type": "object", "properties": PATH_PROPERTY, "required": ["path"],
            "additionalProperties": False,
        },
    },
    {
        "name": "scene_control",
        "description": "Save, run, or stop the current scene.",
        "inputSchema": {
            "type": "object",
            "properties": {"action": {"type": "string", "enum": ["save", "run", "stop"]}},
            "required": ["action"], "additionalProperties": False,
        },
    },
    {
        "name": "start_editor",
        "description": "Start the configured Godot editor for this project.",
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    },
]

TOOL_BY_NAME = {tool["name"]: tool for tool in TOOLS}

# Modes are strict supersets so clients can increase context without losing tools.
TINY_TOOLS = (
    "capabilities", "editor_state", "create_scene", "open_scene", "scene_tree",
    "add_node", "instantiate_scene", "node_info", "set_property", "scene_control",
)
SMALL_TOOLS = TINY_TOOLS + (
    "list_assets", "asset_info", "scan_asset", "import_asset", "create_folder",
    "create_resource",
)
MODE_TOOL_NAMES: dict[Mode, tuple[str, ...]] = {
    "tiny": TINY_TOOLS,
    "small": SMALL_TOOLS,
    "large": SMALL_TOOLS + ("select_node", "start_editor"),
}


def tools_for_mode(mode: Mode) -> list[dict[str, object]]:
    """Return tool schemas in their stable, model-facing order."""
    return [TOOL_BY_NAME[name] for name in MODE_TOOL_NAMES[mode]]
