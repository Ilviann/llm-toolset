"""Declarative MCP tool registry and cross-language contract policy.

Keeping declarative policy separate makes the model-facing API easy to review
without mixing it with transport or execution collaborators.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Literal


LATEST_PROTOCOL = "2025-11-25"
SUPPORTED_PROTOCOLS = frozenset(
    {LATEST_PROTOCOL, "2025-06-18", "2025-03-26", "2024-11-05"}
)

PATH_PROPERTY = {
    "path": {"type": "string", "description": "Scene-relative node path; . is root"}
}
RESOURCE_PATH = {"type": "string", "description": "Project-relative path without res://"}
WAIT_PROPERTIES = {
    "wait": {"type": "boolean", "default": False},
    "timeout_ms": {
        "type": "integer", "minimum": 1, "maximum": 120000, "default": 10000,
    },
}
CURSOR_PROPERTY = {
    "type": "string", "minLength": 48, "maxLength": 48,
    "description": "Opaque continuation cursor",
}
TREE_SCOPE_PROPERTY = {
    "type": "string", "enum": ["edited", "runtime"], "default": "edited",
    "description": "Inspect the editor scene or the active debug run",
}

Mode = Literal["tiny", "small", "large"]
MODES: tuple[Mode, ...] = ("tiny", "small", "large")
ExecutionTarget = Literal["bridge", "assets", "launcher"]
PathKind = Literal[
    "none", "folder", "file", "new_file", "asset_import", "create_folder"
]
WaitStrategy = Literal["none", "scene", "asset", "control", "reload"]


@dataclass(frozen=True)
class ToolSpec:
    """One model-facing tool's schema, exposure, routing, and wait policy."""

    name: str
    description: str
    inputSchema: dict[str, object]
    minimum_mode: Mode
    mode_order: int
    target: ExecutionTarget
    bridge_command: str | None = None
    local_handler: str | None = None
    path_kind: PathKind = "none"
    path_field: str | None = None
    path_extensions: tuple[str, ...] = ()
    wait_strategy: WaitStrategy = "none"

    def mcp_definition(self) -> dict[str, object]:
        return {
            "name": self.name,
            "description": self.description,
            "inputSchema": self.inputSchema,
        }

_TOOL_DEFINITIONS = [
    {
        "name": "capabilities",
        "description": "Get bridge versions, commands, features, and limits.",
        "minimum_mode": "tiny", "mode_order": 0, "target": "bridge",
        "bridge_command": "capabilities",
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    },
    {
        "name": "editor_state",
        "description": "Get Godot version, current scene, selection, and play state.",
        "minimum_mode": "tiny", "mode_order": 1, "target": "bridge",
        "bridge_command": "state",
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    },
    {
        "name": "get_diagnostics",
        "description": "Read bounded editor, parser, and runtime diagnostics.",
        "minimum_mode": "tiny", "mode_order": 2, "target": "bridge",
        "bridge_command": "diagnostics",
        "inputSchema": {
            "type": "object",
            "properties": {
                "scope": {
                    "type": "string", "enum": ["all", "parser", "runtime", "editor"],
                    "default": "all",
                },
                "severity": {
                    "type": "string", "enum": ["error", "warning", "all"],
                    "default": "all",
                },
                "since": {"type": "integer", "minimum": 0, "default": 0},
                "limit": {"type": "integer", "minimum": 1, "maximum": 100, "default": 50},
                "run_id": {"type": "integer", "minimum": 1},
            },
            "additionalProperties": False,
        },
    },
    {
        "name": "reload_project",
        "description": "Safely restart this project and optionally wait for reconnect.",
        "minimum_mode": "tiny", "mode_order": 3, "target": "bridge",
        "bridge_command": "reload_project", "wait_strategy": "reload",
        "inputSchema": {
            "type": "object",
            "properties": {
                "stop_running": {"type": "boolean", "default": False},
                "save_scenes": {"type": "boolean", "default": False},
                **WAIT_PROPERTIES,
            },
            "additionalProperties": False,
        },
    },
    {
        "name": "list_assets",
        "description": "List a bounded page of project assets with stable cursors.",
        "minimum_mode": "small", "mode_order": 12, "target": "bridge",
        "bridge_command": "assets", "path_kind": "folder", "path_field": "folder",
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
                "cursor": CURSOR_PROPERTY,
            },
            "additionalProperties": False,
        },
    },
    {
        "name": "asset_info",
        "description": "Get one asset's type, size, import state, and dependencies.",
        "minimum_mode": "small", "mode_order": 13, "target": "bridge",
        "bridge_command": "asset_info", "path_kind": "file", "path_field": "path",
        "inputSchema": {
            "type": "object", "properties": {"path": RESOURCE_PATH},
            "required": ["path"], "additionalProperties": False,
        },
    },
    {
        "name": "scan_asset",
        "description": "Queue a Godot filesystem scan for one project asset.",
        "minimum_mode": "small", "mode_order": 14, "target": "bridge",
        "bridge_command": "scan_asset", "path_kind": "file", "path_field": "path",
        "wait_strategy": "asset",
        "inputSchema": {
            "type": "object", "properties": {"path": RESOURCE_PATH, **WAIT_PROPERTIES},
            "required": ["path"], "additionalProperties": False,
        },
    },
    {
        "name": "import_asset",
        "description": "Copy one staged file into the project and queue Godot import.",
        "minimum_mode": "small", "mode_order": 15, "target": "assets",
        "local_handler": "import_asset", "path_kind": "asset_import",
        "wait_strategy": "asset",
        "inputSchema": {
            "type": "object",
            "properties": {
                "source": {"type": "string", "description": "Path relative to configured import root"},
                "destination": RESOURCE_PATH,
                **WAIT_PROPERTIES,
            },
            "required": ["source", "destination"], "additionalProperties": False,
        },
    },
    {
        "name": "create_folder",
        "description": "Create a folder inside the Godot project.",
        "minimum_mode": "small", "mode_order": 16, "target": "assets",
        "local_handler": "create_folder", "path_kind": "create_folder",
        "inputSchema": {
            "type": "object", "properties": {"path": RESOURCE_PATH},
            "required": ["path"], "additionalProperties": False,
        },
    },
    {
        "name": "create_resource",
        "description": "Create a whitelisted built-in resource as a text .tres file.",
        "minimum_mode": "small", "mode_order": 17, "target": "bridge",
        "bridge_command": "create_resource", "path_kind": "new_file", "path_field": "path",
        "path_extensions": (".tres",),
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
        "minimum_mode": "tiny", "mode_order": 4, "target": "bridge",
        "bridge_command": "create_scene", "path_kind": "new_file", "path_field": "path",
        "path_extensions": (".tscn",),
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
        "minimum_mode": "tiny", "mode_order": 5, "target": "bridge",
        "bridge_command": "open_scene", "path_kind": "file", "path_field": "path",
        "path_extensions": (".tscn", ".scn"), "wait_strategy": "scene",
        "inputSchema": {
            "type": "object", "properties": {"path": RESOURCE_PATH, **WAIT_PROPERTIES},
            "required": ["path"], "additionalProperties": False,
        },
    },
    {
        "name": "scene_tree",
        "description": "Read a targeted, paginated edited or runtime scene tree.",
        "minimum_mode": "tiny", "mode_order": 6, "target": "bridge",
        "bridge_command": "tree",
        "inputSchema": {
            "type": "object",
            "properties": {
                "tree_scope": TREE_SCOPE_PROPERTY,
                "root": {**PATH_PROPERTY["path"], "default": "."},
                "max_depth": {
                    "type": "integer", "minimum": 0, "maximum": 64, "default": 3,
                },
                "class": {
                    "type": "string", "description": "Exact Godot node class filter",
                },
                "limit": {"type": "integer", "minimum": 1, "maximum": 200, "default": 50},
                "cursor": CURSOR_PROPERTY,
            },
            "additionalProperties": False,
        },
    },
    {
        "name": "add_node",
        "description": "Add a built-in node to the edited scene through undo history.",
        "minimum_mode": "tiny", "mode_order": 7, "target": "bridge",
        "bridge_command": "add_node",
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
        "minimum_mode": "tiny", "mode_order": 8, "target": "bridge",
        "bridge_command": "instantiate_scene", "path_kind": "file", "path_field": "scene",
        "path_extensions": (".tscn", ".scn"),
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
        "description": "Read filtered properties of one edited or runtime scene node.",
        "minimum_mode": "tiny", "mode_order": 9, "target": "bridge",
        "bridge_command": "inspect",
        "inputSchema": {
            "type": "object",
            "properties": {
                "tree_scope": TREE_SCOPE_PROPERTY,
                **PATH_PROPERTY,
                "runtime_id": {
                    "type": "string", "minLength": 64, "maxLength": 64,
                    "description": "Optional runtime identity returned by scene_tree",
                },
                "property": {"type": "string", "description": "Exact property-name filter"},
                "category": {"type": "string", "description": "Exact Godot category filter"},
                "limit": {"type": "integer", "minimum": 1, "maximum": 64, "default": 24},
                "cursor": CURSOR_PROPERTY,
            },
            "required": ["path"],
            "additionalProperties": False,
        },
    },
    {
        "name": "set_property",
        "description": "Set one node property through Godot undo history.",
        "minimum_mode": "tiny", "mode_order": 10, "target": "bridge",
        "bridge_command": "set_property",
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
        "minimum_mode": "large", "mode_order": 24, "target": "bridge",
        "bridge_command": "select",
        "inputSchema": {
            "type": "object", "properties": PATH_PROPERTY, "required": ["path"],
            "additionalProperties": False,
        },
    },
    {
        "name": "scene_control",
        "description": "Save or run the scene; stop requires its current run ID.",
        "minimum_mode": "tiny", "mode_order": 11, "target": "bridge",
        "bridge_command": "control", "wait_strategy": "control",
        "inputSchema": {
            "type": "object",
            "properties": {
                "action": {"type": "string", "enum": ["save", "run", "stop"]},
                "run_id": {
                    "type": "integer",
                    "minimum": 1,
                    "description": "Required for stop; use the ID returned by run.",
                },
                **WAIT_PROPERTIES,
                "startup_window_ms": {
                    "type": "integer", "minimum": 0, "maximum": 5000, "default": 250,
                    "description": "Run health window; used only when action is run.",
                },
            },
            "required": ["action"], "additionalProperties": False,
        },
    },
    {
        "name": "capture_game_view",
        "description": "Capture the active run's main viewport as a bounded PNG image.",
        "minimum_mode": "small", "mode_order": 21, "target": "bridge",
        "bridge_command": "capture_game_view",
        "inputSchema": {
            "type": "object",
            "properties": {
                "run_id": {"type": "integer", "minimum": 1},
                "max_width": {
                    "type": "integer", "minimum": 1, "maximum": 2048,
                    "default": 1280,
                },
                "max_height": {
                    "type": "integer", "minimum": 1, "maximum": 2048,
                    "default": 720,
                },
            },
            "required": ["run_id"], "additionalProperties": False,
        },
    },
    {
        "name": "send_input",
        "description": "Inject one bounded Input Map action into the active run.",
        "minimum_mode": "small", "mode_order": 22, "target": "bridge",
        "bridge_command": "send_input",
        "inputSchema": {
            "type": "object",
            "properties": {
                "run_id": {"type": "integer", "minimum": 1},
                "action": {"type": "string", "minLength": 1, "maxLength": 128},
                "pressed": {"type": "boolean", "default": True},
                "strength": {
                    "type": "number", "minimum": 0, "maximum": 1,
                    "default": 1,
                },
                "duration_ms": {"type": "integer", "minimum": 1, "maximum": 10000},
                "frames": {"type": "integer", "minimum": 1, "maximum": 600},
            },
            "required": ["run_id", "action"], "additionalProperties": False,
        },
    },
    {
        "name": "wait_for_runtime_condition",
        "description": "Wait for one bounded runtime play, node, count, or property condition.",
        "minimum_mode": "small", "mode_order": 23, "target": "bridge",
        "bridge_command": "wait_runtime_condition",
        "inputSchema": {
            "type": "object",
            "properties": {
                "scope": {"type": "string", "enum": ["runtime"]},
                "run_id": {"type": "integer", "minimum": 1},
                "condition": {
                    "type": "string",
                    "enum": ["play_state", "node_exists", "node_count", "property"],
                },
                "expected_state": {"type": "string", "enum": ["running", "stopped"]},
                "path": {**PATH_PROPERTY["path"], "default": "."},
                "exists": {"type": "boolean", "default": True},
                "group": {"type": "string", "minLength": 1, "maxLength": 128},
                "max_depth": {
                    "type": "integer", "minimum": 0, "maximum": 64, "default": 64,
                },
                "property": {"type": "string", "minLength": 1, "maxLength": 128},
                "comparison": {
                    "type": "string", "enum": ["eq", "ne", "lt", "lte", "gt", "gte"],
                    "default": "eq",
                },
                "value": {"description": "Scalar comparison value"},
                "timeout_ms": {
                    "type": "integer", "minimum": 1, "maximum": 10000,
                    "default": 1000,
                },
            },
            "required": ["scope", "run_id", "condition"],
            "additionalProperties": False,
        },
    },
    {
        "name": "project_settings_get",
        "description": "Read one project setting or a bounded setting prefix.",
        "minimum_mode": "small", "mode_order": 18, "target": "bridge",
        "bridge_command": "project_settings_get",
        "inputSchema": {
            "type": "object",
            "properties": {
                "key": {"type": "string", "description": "Setting key or prefix"},
                "recursive": {"type": "boolean", "default": False},
            },
            "required": ["key"], "additionalProperties": False,
        },
    },
    {
        "name": "project_settings_patch",
        "description": "Atomically validate, compare, and patch project settings.",
        "minimum_mode": "small", "mode_order": 19, "target": "bridge",
        "bridge_command": "project_settings_patch",
        "inputSchema": {
            "type": "object",
            "properties": {
                "changes": {
                    "type": "array", "minItems": 1, "maxItems": 32,
                    "items": {
                        "type": "object",
                        "properties": {
                            "key": {"type": "string"},
                            "expected": {"description": "Optional compare-and-swap value"},
                            "value": {"description": "JSON-safe project setting value"},
                        },
                        "required": ["key", "value"], "additionalProperties": False,
                    },
                },
                "save": {"type": "boolean", "default": True},
                "dry_run": {"type": "boolean", "default": False},
            },
            "required": ["changes"], "additionalProperties": False,
        },
    },
    {
        "name": "input_map_patch",
        "description": "Add or remove normalized Input Map bindings without duplicates.",
        "minimum_mode": "small", "mode_order": 20, "target": "bridge",
        "bridge_command": "input_map_patch",
        "inputSchema": {
            "type": "object",
            "properties": {
                "action": {"type": "string"},
                "deadzone": {"type": "number", "minimum": 0, "maximum": 1},
                "add_events": {
                    "type": "array", "maxItems": 32,
                    "items": {"$ref": "#/$defs/event"}, "default": [],
                },
                "remove_events": {
                    "type": "array", "maxItems": 32,
                    "items": {"$ref": "#/$defs/event"}, "default": [],
                },
                "save": {"type": "boolean", "default": True},
                "dry_run": {"type": "boolean", "default": False},
            },
            "required": ["action"], "additionalProperties": False,
            "$defs": {
                "event": {
                    "type": "object",
                    "description": "key, mouse_button, joypad_button, or joypad_motion event",
                    "properties": {
                        "type": {"type": "string", "enum": [
                            "key", "mouse_button", "joypad_button", "joypad_motion"
                        ]},
                        "key": {"description": "Key name or numeric Godot keycode"},
                        "physical": {"type": "boolean", "default": False},
                        "button": {"description": "Button name or index"},
                        "axis": {"description": "Axis name or index"},
                        "direction": {"type": "number", "enum": [-1, 1]},
                        "device": {"type": "integer", "minimum": -1, "maximum": 32, "default": -1},
                        "shift": {"type": "boolean", "default": False},
                        "alt": {"type": "boolean", "default": False},
                        "ctrl": {"type": "boolean", "default": False},
                        "meta": {"type": "boolean", "default": False},
                    },
                    "required": ["type"], "additionalProperties": False,
                }
            },
        },
    },
    {
        "name": "start_editor",
        "description": "Start the configured Godot editor for this project.",
        "minimum_mode": "large", "mode_order": 25, "target": "launcher",
        "local_handler": "start_editor",
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    },
]

TOOL_SPECS = tuple(ToolSpec(**definition) for definition in _TOOL_DEFINITIONS)
SPEC_BY_NAME = {spec.name: spec for spec in TOOL_SPECS}

# Preserve the original public schema exports while deriving them from the registry.
TOOLS = [spec.mcp_definition() for spec in TOOL_SPECS]
TOOL_BY_NAME = {tool["name"]: tool for tool in TOOLS}

_MODE_RANK = {mode: index for index, mode in enumerate(MODES)}
MODE_TOOL_NAMES: dict[Mode, tuple[str, ...]] = {
    mode: tuple(
        spec.name
        for spec in sorted(TOOL_SPECS, key=lambda item: item.mode_order)
        if _MODE_RANK[spec.minimum_mode] <= _MODE_RANK[mode]
    )
    for mode in MODES
}
TINY_TOOLS = MODE_TOOL_NAMES["tiny"]
SMALL_TOOLS = MODE_TOOL_NAMES["small"]

BRIDGE_COMMANDS = {
    spec.name: spec.bridge_command
    for spec in TOOL_SPECS
    if spec.target == "bridge" and spec.bridge_command is not None
}
WAIT_AWARE_TOOLS = frozenset(
    spec.name for spec in TOOL_SPECS if spec.wait_strategy != "none"
)
INTERNAL_BRIDGE_COMMANDS = ("reload_status",)
EXPECTED_BRIDGE_COMMANDS = tuple(sorted({*BRIDGE_COMMANDS.values(), *INTERNAL_BRIDGE_COMMANDS}))
BRIDGE_PROTOCOL_VERSION = "1"
EXPECTED_BRIDGE_LIMITS = {
    "request_bytes": 64 * 1024,
    "response_bytes": 256 * 1024,
    "tree_nodes": 200,
    "tree_depth": 64,
    "tree_scan": 5000,
    "properties": 64,
    "property_scan": 1024,
    "assets": 100,
    "asset_scan": 5000,
    "active_cursors": 128,
    "cursor_chars": 48,
    "cursor_ttl_ms": 120000,
    "settings": 100,
    "setting_changes": 32,
    "input_events": 32,
    "diagnostics": 100,
    "diagnostic_records": 256,
    "runtime_pending_requests": 16,
    "runtime_request_timeout_ms": 2000,
    "runtime_groups": 8,
    "capture_source_width": 8192,
    "capture_source_height": 8192,
    "capture_source_pixels": 33554432,
    "capture_output_width": 2048,
    "capture_output_height": 2048,
    "capture_output_pixels": 4194304,
    "capture_bytes": 8388608,
    "capture_timeout_ms": 2000,
    "concurrent_inputs": 8,
    "input_duration_ms": 10000,
    "input_frames": 600,
    "condition_timeout_ms": 10000,
    "condition_evidence": 8,
}
EXPECTED_EDITOR_ERROR_CODES = (
    "unauthorized", "invalid_argument", "protected_path", "not_found",
    "editor_busy", "import_pending", "no_active_run", "stale_runtime_id",
    "timeout", "unsupported_capability", "stale_cursor", "project_mismatch",
    "save_failed", "malformed_operation", "stale_operation", "version_mismatch",
    "runtime_probe_unavailable", "ambiguous_runtime_session",
)


def bridge_contract_mismatches(
    capabilities: object, *, expected_version: str,
) -> list[str]:
    """Compare a live plugin capability response with the Python contract."""
    if not isinstance(capabilities, dict):
        return ["capabilities must be an object"]
    expected = {
        "bridge_version": expected_version,
        "bridge_protocol_version": BRIDGE_PROTOCOL_VERSION,
        "commands": list(EXPECTED_BRIDGE_COMMANDS),
        "limits": EXPECTED_BRIDGE_LIMITS,
        "error_codes": list(EXPECTED_EDITOR_ERROR_CODES),
    }
    return [
        f"{field}: expected {value!r}, got {capabilities.get(field)!r}"
        for field, value in expected.items()
        if capabilities.get(field) != value
    ]


def tools_for_mode(mode: Mode) -> list[dict[str, object]]:
    """Return tool schemas in their stable, model-facing order."""
    return [TOOL_BY_NAME[name] for name in MODE_TOOL_NAMES[mode]]
