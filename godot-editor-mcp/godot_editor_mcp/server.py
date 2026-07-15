"""MCP request handling for the Godot editor bridge.

The public imports in this module are retained for compatibility. Static tool
schemas live in :mod:`tool_catalog`, execution in :mod:`tool_dispatch`, stdio
transport in :mod:`stdio`, and command-line setup in :mod:`cli`.
"""

from __future__ import annotations

from typing import Any

from . import __version__
from .errors import DomainError
from .stdio import error, result, tool_result
from .tool_catalog import (
    LATEST_PROTOCOL,
    MODES,
    MODE_TOOL_NAMES,
    PATH_PROPERTY,
    RESOURCE_PATH,
    SMALL_TOOLS,
    SUPPORTED_PROTOCOLS,
    TINY_TOOLS,
    TOOL_BY_NAME,
    TOOLS,
    Mode,
    tools_for_mode,
)
from .tool_dispatch import (
    AssetManager,
    BridgeClient,
    EditorStarter,
    ToolDispatcher,
)


class MCPServer:
    """Validate MCP requests and delegate permitted tool calls."""

    def __init__(
        self,
        bridge: BridgeClient,
        assets: AssetManager | None = None,
        *,
        mode: Mode = "tiny",
        launcher: EditorStarter | None = None,
    ) -> None:
        if mode not in MODES:
            raise ValueError(f"Mode must be one of: {', '.join(MODES)}")
        self.bridge = bridge
        self.assets = assets
        self.mode = mode
        self.launcher = launcher
        self.tool_names = MODE_TOOL_NAMES[mode]
        self.tools = tools_for_mode(mode)
        self.negotiated_protocol_version: str | None = None
        self._dispatcher = ToolDispatcher(
            bridge, assets, mode=mode, launcher=launcher
        )

    # Preserve these helpers for callers that used the earlier server module.
    _result = staticmethod(result)
    _error = staticmethod(error)
    _tool_result = staticmethod(tool_result)

    def handle(self, message: dict[str, Any]) -> dict[str, Any] | None:
        request_id = message.get("id")
        method = message.get("method")
        params = message.get("params") or {}
        if "id" not in message:
            return None
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
                "capabilities": {"tools": {}},
                "serverInfo": {
                    "name": "godot-editor", "version": __version__, "mode": self.mode,
                },
            })
        if method == "ping":
            return result(request_id, {})
        if method == "tools/list":
            return result(request_id, {"tools": self.tools})
        if method == "tools/call":
            return self._call_tool(request_id, params)
        return error(request_id, -32601, "Method not found")

    def _call_tool(self, request_id: Any, params: dict[str, Any]) -> dict[str, Any]:
        name = params.get("name")
        arguments = params.get("arguments") or {}
        if not isinstance(arguments, dict):
            return error(request_id, -32602, "Invalid tool arguments")
        if name not in self.tool_names:
            return error(
                request_id, -32602, f"Tool is unavailable in {self.mode} mode"
            )
        try:
            output = self._dispatcher.call(name, arguments)
            if name == "capabilities" and isinstance(output, dict):
                output = {
                    **output,
                    "mcp_protocol_version": self.negotiated_protocol_version,
                }
            return result(request_id, tool_result(output))
        except DomainError as exc:
            return result(request_id, tool_result(exc.as_dict(), is_error=True))

    def close(self) -> None:
        """Cancel outstanding waits during stdio or host shutdown."""
        self._dispatcher.close()


def run(
    bridge: BridgeClient,
    assets: AssetManager,
    *,
    mode: Mode = "tiny",
    launcher: EditorStarter | None = None,
) -> None:
    """Compatibility wrapper for the original server module entry point."""
    from .cli import run as run_cli

    run_cli(bridge, assets, mode=mode, launcher=launcher)


def main() -> None:
    """Compatibility wrapper for MCP hosts using ``server:main``."""
    from .cli import main as cli_main

    cli_main()


__all__ = [
    "LATEST_PROTOCOL",
    "MODES",
    "MODE_TOOL_NAMES",
    "MCPServer",
    "PATH_PROPERTY",
    "RESOURCE_PATH",
    "SMALL_TOOLS",
    "SUPPORTED_PROTOCOLS",
    "TINY_TOOLS",
    "TOOL_BY_NAME",
    "TOOLS",
    "main",
    "run",
]
