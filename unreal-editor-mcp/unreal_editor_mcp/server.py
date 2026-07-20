"""MCP initialization, tool catalog, schema checks, and dispatch."""

from __future__ import annotations

from typing import Any, Protocol

from . import __version__
from .errors import DomainError
from .schema_validation import SchemaValidationError, validate_tool_arguments
from .stdio import error, result, tool_result
from .tool_catalog import LATEST_PROTOCOL, SUPPORTED_PROTOCOLS, TOOL_BY_NAME, TOOLS


class BridgeClient(Protocol):
    def call(self, command: str, arguments: dict[str, Any] | None = None) -> Any: ...
    def close(self) -> None: ...


class MCPServer:
    def __init__(self, bridge: BridgeClient) -> None:
        self.bridge = bridge
        self.negotiated_protocol_version: str | None = None

    def handle(self, message: dict[str, Any]) -> dict[str, Any] | None:
        if "id" not in message:
            return None
        request_id = message.get("id")
        method = message.get("method")
        params = message.get("params", {})
        if message.get("jsonrpc") != "2.0" or not isinstance(method, str):
            return error(request_id, -32600, "Invalid Request")
        if not isinstance(params, dict):
            return error(request_id, -32602, "Invalid params")
        if method == "initialize":
            requested = params.get("protocolVersion")
            protocol = requested if requested in SUPPORTED_PROTOCOLS else LATEST_PROTOCOL
            self.negotiated_protocol_version = protocol
            return result(request_id, {
                "protocolVersion": protocol,
                "capabilities": {"tools": {"listChanged": False}},
                "serverInfo": {"name": "unreal-editor", "version": __version__},
            })
        if method == "ping":
            return result(request_id, {})
        if method == "tools/list":
            return result(request_id, {"tools": list(TOOLS)})
        if method == "tools/call":
            return self._call_tool(request_id, params)
        return error(request_id, -32601, "Method not found")

    def _call_tool(self, request_id: Any, params: dict[str, Any]) -> dict[str, Any]:
        name = params.get("name")
        arguments = params.get("arguments", {})
        if not isinstance(name, str) or name not in TOOL_BY_NAME:
            return error(request_id, -32602, "Unknown tool")
        if not isinstance(arguments, dict):
            return error(request_id, -32602, "Invalid tool arguments")
        try:
            validate_tool_arguments(arguments, TOOL_BY_NAME[name]["inputSchema"])
        except SchemaValidationError as exc:
            return error(request_id, -32602, f"Invalid tool arguments: {exc}")
        try:
            output = self.bridge.call(name, arguments)
            if name == "capabilities" and isinstance(output, dict):
                bridge_version = output.get("bridge_version")
                output = {
                    **output,
                    "python_version": __version__,
                    "version_match": bridge_version == __version__,
                    "mcp_protocol_version": self.negotiated_protocol_version,
                }
            return result(request_id, tool_result(output))
        except DomainError as exc:
            return result(request_id, tool_result(exc.as_dict(), is_error=True))

    def close(self) -> None:
        self.bridge.close()
