"""MCP initialization, tool catalog, schema checks, and dispatch."""

from __future__ import annotations

from typing import Any, Protocol

from . import __version__
from .errors import DomainError, ErrorCode
from .extension_catalog import compose_companion_capabilities, compose_extension_tools
from .project import ProjectIdentity
from .schema_validation import SchemaValidationError, validate_tool_arguments
from .stdio import error, result, tool_result
from .tool_catalog import LATEST_PROTOCOL, SUPPORTED_PROTOCOLS, tools_for_configuration


class BridgeClient(Protocol):
    def call(self, command: str, arguments: dict[str, Any] | None = None) -> Any: ...
    def close(self) -> None: ...


class LifecycleClient(Protocol):
    def availability(self) -> dict[str, Any]: ...
    def execute(self, arguments: dict[str, Any]) -> dict[str, Any]: ...
    def close(self) -> None: ...


class MCPServer:
    def __init__(
        self,
        bridge: BridgeClient,
        *,
        project_identity: ProjectIdentity | None = None,
        lifecycle: LifecycleClient | None = None,
        writable: bool = False,
    ) -> None:
        if type(writable) is not bool:
            raise TypeError("writable must be Boolean")
        self.bridge = bridge
        self.project_identity = project_identity
        self.lifecycle = lifecycle
        self.writable = writable
        self.tools = tools_for_configuration(
            writable=writable,
            lifecycle_enabled=lifecycle is not None,
        )
        self.tool_by_name = {tool["name"]: tool for tool in self.tools}
        self._notifications: list[dict[str, Any]] = []
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
                "capabilities": {"tools": {"listChanged": True}},
                "serverInfo": {"name": "unreal-editor", "version": __version__},
            })
        if method == "ping":
            return result(request_id, {})
        if method == "tools/list":
            self._refresh_extension_tools()
            return result(request_id, {"tools": list(self.tools)})
        if method == "tools/call":
            return self._call_tool(request_id, params)
        return error(request_id, -32601, "Method not found")

    def _call_tool(self, request_id: Any, params: dict[str, Any]) -> dict[str, Any]:
        name = params.get("name")
        arguments = params.get("arguments", {})
        if not isinstance(name, str) or name not in self.tool_by_name:
            return error(request_id, -32602, "Unknown tool")
        if not isinstance(arguments, dict):
            return error(request_id, -32602, "Invalid tool arguments")
        if isinstance(arguments.get("extension_id"), str):
            self._refresh_extension_tools()
        try:
            validate_tool_arguments(arguments, self.tool_by_name[name]["inputSchema"])
        except SchemaValidationError as exc:
            return error(request_id, -32602, f"Invalid tool arguments: {exc}")
        try:
            if name == "editor_lifecycle":
                if self.lifecycle is None:
                    raise DomainError(
                        "Editor lifecycle is unavailable in this server configuration",
                        code=ErrorCode.INVALID_CONFIGURATION,
                    )
                output = self.lifecycle.execute(arguments)
                self._refresh_extension_tools(notify=True)
            elif name == "capabilities":
                output = self._capabilities(arguments)
            else:
                output = self.bridge.call(name, arguments)
            return result(request_id, tool_result(output))
        except DomainError as exc:
            return result(request_id, tool_result(exc.as_dict(), is_error=True))

    def _capabilities(self, arguments: dict[str, Any]) -> Any:
        native_available = True
        try:
            output = self.bridge.call("capabilities", arguments)
        except DomainError as exc:
            if exc.code != ErrorCode.EDITOR_UNAVAILABLE or self.project_identity is None:
                raise
            native_available = False
            output = {}
        if not isinstance(output, dict):
            return output

        local = {
            "python_version": __version__,
            "mcp_protocol_version": self.negotiated_protocol_version,
            "access_mode": "writable" if self.writable else "readonly",
            "native_capabilities_available": native_available,
            "editor_lifecycle": (
                self.lifecycle.availability()
                if self.lifecycle is not None
                else {"enabled": False, "launch_configured": False}
            ),
        }
        if self.project_identity is not None:
            local.update({
                "project_name": self.project_identity.name,
                "project_hash": self.project_identity.project_hash,
            })
        if native_available:
            local["version_match"] = output.get("bridge_version") == __version__
            compose_companion_capabilities(output)
            self._set_extension_tools(output, notify=True)
        else:
            local["bridge_ready"] = False
            self._set_extension_tools({}, notify=True)
        return {**output, **local}

    def _refresh_extension_tools(self, *, notify: bool = False) -> None:
        try:
            native = self.bridge.call("capabilities", {})
        except DomainError:
            native = {}
        self._set_extension_tools(native, notify=notify)

    def _set_extension_tools(self, native: object, *, notify: bool) -> None:
        base = tools_for_configuration(
            writable=self.writable,
            lifecycle_enabled=self.lifecycle is not None,
        )
        updated = compose_extension_tools(base, native, writable=self.writable)
        changed = updated != self.tools
        self.tools = updated
        self.tool_by_name = {tool["name"]: tool for tool in self.tools}
        if changed and notify:
            self._notifications.append({"jsonrpc": "2.0", "method": "notifications/tools/list_changed"})

    def drain_notifications(self) -> list[dict[str, Any]]:
        notifications = self._notifications
        self._notifications = []
        return notifications

    def close(self) -> None:
        if self.lifecycle is not None:
            self.lifecycle.close()
        self.bridge.close()
