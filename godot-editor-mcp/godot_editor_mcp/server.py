"""Dependency-free newline-delimited JSON-RPC MCP server."""

from __future__ import annotations

import argparse
import json
import sys
from typing import Any

from . import __version__
from .bridge import BridgeError, GodotBridge


LATEST_PROTOCOL = "2025-11-25"
SUPPORTED_PROTOCOLS = {LATEST_PROTOCOL, "2025-06-18", "2025-03-26", "2024-11-05"}

PATH_PROPERTY = {"path": {"type": "string", "description": "Scene-relative node path; . is root"}}

TOOLS = [
    {
        "name": "editor_state",
        "description": "Get Godot version, current scene, selection, and play state.",
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    },
    {
        "name": "scene_tree",
        "description": "List the edited scene tree, limited to 200 nodes.",
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
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
    def __init__(self, bridge: GodotBridge) -> None:
        self.bridge = bridge

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
            "editor_state": "state", "scene_tree": "tree", "node_info": "inspect",
            "set_property": "set_property", "select_node": "select", "scene_control": "control",
        }
        command = commands.get(name)
        if command is None:
            return self._error(request_id, -32602, "Unknown tool")
        try:
            output = self.bridge.call(command, arguments)
            return self._result(request_id, self._tool_result(output))
        except (BridgeError, TypeError, ValueError) as exc:
            return self._result(request_id, self._tool_result(str(exc), is_error=True))


def run(bridge: GodotBridge) -> None:
    server = MCPServer(bridge)
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
    args = parser.parse_args()
    try:
        run(GodotBridge(args.project, port=args.port))
    except BridgeError as exc:
        parser.error(str(exc))
