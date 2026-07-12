"""Dependency-free newline-delimited JSON-RPC MCP server."""

from __future__ import annotations

import argparse
import json
import sys
from typing import Any

from . import __version__
from .assets import AssetError, ProjectAssets
from .bridge import BridgeError, GodotBridge


LATEST_PROTOCOL = "2025-11-25"
SUPPORTED_PROTOCOLS = {LATEST_PROTOCOL, "2025-06-18", "2025-03-26", "2024-11-05"}

PATH_PROPERTY = {"path": {"type": "string", "description": "Scene-relative node path; . is root"}}
RESOURCE_PATH = {"type": "string", "description": "Project-relative path without res://"}

TOOLS = [
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
                    "enum": ["all", "scene", "script", "image", "model", "audio", "font", "material", "resource"],
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
]


class MCPServer:
    def __init__(self, bridge: GodotBridge, assets: ProjectAssets | None = None) -> None:
        self.bridge = bridge
        self.assets = assets

    @staticmethod
    def _result(request_id: Any, result: dict[str, Any]) -> dict[str, Any]:
        return {"jsonrpc": "2.0", "id": request_id, "result": result}

    @staticmethod
    def _error(request_id: Any, code: int, message: str) -> dict[str, Any]:
        return {"jsonrpc": "2.0", "id": request_id, "error": {"code": code, "message": message}}

    @staticmethod
    def _tool_result(value: Any, *, is_error: bool = False) -> dict[str, Any]:
        text = value if isinstance(value, str) else json.dumps(
            value, ensure_ascii=False, separators=(",", ":"), sort_keys=True
        )
        result: dict[str, Any] = {"content": [{"type": "text", "text": text}]}
        if is_error:
            result["isError"] = True
        return result

    def handle(self, message: dict[str, Any]) -> dict[str, Any] | None:
        request_id = message.get("id")
        method = message.get("method")
        params = message.get("params") or {}
        if "id" not in message:
            return None
        if message.get("jsonrpc") != "2.0" or not isinstance(method, str):
            return self._error(request_id, -32600, "Invalid Request")
        if not isinstance(params, dict):
            return self._error(request_id, -32602, "Invalid params")

        if method == "initialize":
            requested = params.get("protocolVersion")
            protocol = requested if requested in SUPPORTED_PROTOCOLS else LATEST_PROTOCOL
            return self._result(request_id, {
                "protocolVersion": protocol,
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "godot-editor", "version": __version__},
            })
        if method == "ping":
            return self._result(request_id, {})
        if method == "tools/list":
            return self._result(request_id, {"tools": TOOLS})
        if method == "tools/call":
            return self._call_tool(request_id, params)
        return self._error(request_id, -32601, "Method not found")

    def _call_tool(self, request_id: Any, params: dict[str, Any]) -> dict[str, Any]:
        name = params.get("name")
        arguments = params.get("arguments") or {}
        if not isinstance(arguments, dict):
            return self._error(request_id, -32602, "Invalid tool arguments")
        commands = {
            "editor_state": "state", "list_assets": "assets", "asset_info": "asset_info",
            "create_resource": "create_resource", "create_scene": "create_scene",
            "open_scene": "open_scene",
            "scene_tree": "tree", "node_info": "inspect", "add_node": "add_node",
            "instantiate_scene": "instantiate_scene", "set_property": "set_property",
            "select_node": "select", "scene_control": "control",
        }
        try:
            if name == "import_asset":
                if self.assets is None:
                    raise AssetError("Asset import is unavailable")
                output = self.assets.import_asset(
                    arguments.get("source"), arguments.get("destination")
                )
                self._queue_scan(output, output["destination"])
            elif name == "create_folder":
                if self.assets is None:
                    raise AssetError("Folder creation is unavailable")
                output = self.assets.create_folder(arguments.get("path"))
                self._queue_scan(output, output["path"])
            else:
                command = commands.get(name)
                if command is None:
                    return self._error(request_id, -32602, "Unknown tool")
                if self.assets is not None:
                    if name == "list_assets":
                        self.assets.validate_folder(arguments.get("folder", "."))
                    elif name == "asset_info":
                        self.assets.validate_file(arguments.get("path"))
                    elif name == "create_scene":
                        self.assets.validate_new_file(arguments.get("path"), {".tscn"})
                    elif name == "create_resource":
                        self.assets.validate_new_file(arguments.get("path"), {".tres"})
                    elif name == "open_scene":
                        self.assets.validate_file(arguments.get("path"), {".tscn", ".scn"})
                    elif name == "instantiate_scene":
                        self.assets.validate_file(arguments.get("scene"), {".tscn", ".scn"})
                output = self.bridge.call(command, arguments)
            return self._result(request_id, self._tool_result(output))
        except (AssetError, BridgeError, TypeError, ValueError) as exc:
            return self._result(request_id, self._tool_result(str(exc), is_error=True))

    def _queue_scan(self, output: dict[str, Any], path: str) -> None:
        try:
            scan = self.bridge.call(
                "scan_asset", {"path": path.removeprefix("res://")}
            )
            output["scan"] = scan.get("scan", "queued") if isinstance(scan, dict) else "queued"
        except BridgeError as exc:
            output["scan"] = "pending"
            output["warning"] = str(exc)


def run(bridge: GodotBridge, assets: ProjectAssets) -> None:
    server = MCPServer(bridge, assets)
    for line in sys.stdin:
        try:
            message = json.loads(line)
            response = server.handle(message) if isinstance(message, dict) else server._error(None, -32600, "Invalid Request")
        except json.JSONDecodeError:
            response = server._error(None, -32700, "Parse error")
        except Exception as exc:
            print(f"godot-editor-mcp: {exc}", file=sys.stderr)
            response = server._error(None, -32603, "Internal error")
        if response is not None:
            print(json.dumps(response, ensure_ascii=False, separators=(",", ":")), flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description="Small MCP bridge for the Godot 4 editor")
    parser.add_argument("project", help="Godot project folder")
    parser.add_argument("--port", type=int, default=6505, help="Plugin port (default: 6505)")
    parser.add_argument("--import-root", help="Folder containing assets staged for import")
    args = parser.parse_args()
    try:
        bridge = GodotBridge(args.project, port=args.port)
        run(bridge, ProjectAssets(args.project, args.import_root))
    except (AssetError, BridgeError) as exc:
        parser.error(str(exc))
