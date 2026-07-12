"""Minimal dependency-free MCP server using newline-delimited JSON-RPC."""

from __future__ import annotations

import argparse
import json
import sys
from typing import Any

from . import __version__
from .filesystem import FileAccessError, RootedFilesystem


LATEST_PROTOCOL = "2025-11-25"
SUPPORTED_PROTOCOLS = {LATEST_PROTOCOL, "2025-06-18", "2025-03-26", "2024-11-05"}

TOOLS = [
    {
        "name": "list_dir",
        "description": "List direct entries. Paths are relative to root.",
        "inputSchema": {
            "type": "object",
            "properties": {"path": {"type": "string", "default": "."}},
            "additionalProperties": False,
        },
    },
    {
        "name": "tree",
        "description": "Show a folder tree, up to 100 entries.",
        "inputSchema": {
            "type": "object",
            "properties": {"path": {"type": "string", "default": "."}},
            "additionalProperties": False,
        },
    },
    {
        "name": "read_text",
        "description": "Read a UTF-8 text file. Binary files are denied.",
        "inputSchema": {
            "type": "object",
            "properties": {"path": {"type": "string"}},
            "required": ["path"],
            "additionalProperties": False,
        },
    },
    {
        "name": "write_text",
        "description": "Create or replace a UTF-8 text file.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "path": {"type": "string"},
                "content": {"type": "string"},
            },
            "required": ["path", "content"],
            "additionalProperties": False,
        },
    },
]


class MCPServer:
    def __init__(self, filesystem: RootedFilesystem) -> None:
        self.fs = filesystem

    @staticmethod
    def _result(request_id: Any, result: dict[str, Any]) -> dict[str, Any]:
        return {"jsonrpc": "2.0", "id": request_id, "result": result}

    @staticmethod
    def _error(request_id: Any, code: int, message: str) -> dict[str, Any]:
        return {
            "jsonrpc": "2.0",
            "id": request_id,
            "error": {"code": code, "message": message},
        }

    @staticmethod
    def _tool_result(text: str, *, is_error: bool = False) -> dict[str, Any]:
        result: dict[str, Any] = {"content": [{"type": "text", "text": text}]}
        if is_error:
            result["isError"] = True
        return result

    def handle(self, message: dict[str, Any]) -> dict[str, Any] | None:
        request_id = message.get("id")
        method = message.get("method")
        params = message.get("params") or {}

        # Notifications do not receive responses.
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
                "serverInfo": {
                    "name": "rooted-files",
                    "version": __version__,
                    "description": "Root-confined text file tools",
                },
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
        try:
            if name == "list_dir":
                output = self.fs.list_dir(arguments.get("path", "."))
            elif name == "tree":
                output = self.fs.tree(arguments.get("path", "."))
            elif name == "read_text":
                output = self.fs.read_text(arguments["path"])
            elif name == "write_text":
                output = self.fs.write_text(arguments["path"], arguments["content"])
            else:
                return self._error(request_id, -32602, "Unknown tool")
            return self._result(request_id, self._tool_result(output))
        except KeyError as exc:
            result = self._tool_result(f"Missing argument: {exc.args[0]}", is_error=True)
            return self._result(request_id, result)
        except (FileAccessError, TypeError) as exc:
            return self._result(request_id, self._tool_result(str(exc), is_error=True))


def run(root: str) -> None:
    server = MCPServer(RootedFilesystem(root))
    for line in sys.stdin:
        try:
            message = json.loads(line)
            if not isinstance(message, dict):
                response = MCPServer._error(None, -32600, "Invalid Request")
            else:
                response = server.handle(message)
        except json.JSONDecodeError:
            response = MCPServer._error(None, -32700, "Parse error")
        except Exception as exc:  # Keep the subprocess alive on an unexpected request.
            print(f"rooted-files-mcp: {exc}", file=sys.stderr)
            response = MCPServer._error(None, -32603, "Internal error")
        if response is not None:
            print(json.dumps(response, ensure_ascii=False, separators=(",", ":")), flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description="Root-confined text file MCP server")
    parser.add_argument("root", help="Folder exposed as MCP root")
    args = parser.parse_args()
    try:
        run(args.root)
    except FileAccessError as exc:
        parser.error(str(exc))


if __name__ == "__main__":
    main()

