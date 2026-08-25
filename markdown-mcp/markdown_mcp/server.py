"""Minimal dependency-free MCP server using newline-delimited JSON-RPC."""

from __future__ import annotations

import json
import sys
from typing import Any, Sequence

from . import __version__
from .configuration import Settings, load_settings
from .filesystem import FileAccessError, MAX_MARKDOWN_BYTES, MarkdownFilesystem


LATEST_PROTOCOL = "2025-11-25"
SUPPORTED_PROTOCOLS = {
    LATEST_PROTOCOL,
    "2025-06-18",
    "2025-03-26",
    "2024-11-05",
}
MAX_TITLE_CHARS = 1000


def _string_schema(*, max_length: int) -> dict[str, Any]:
    return {"type": "string", "maxLength": max_length}


TOOLS: tuple[dict[str, Any], ...] = (
    {
        "name": "read_markdown",
        "description": "Read a validated Markdown file or exact #section/#--- block.",
        "inputSchema": {
            "type": "object",
            "properties": {"path": _string_schema(max_length=4096)},
            "required": ["path"],
            "additionalProperties": False,
        },
    },
    {
        "name": "list_sections",
        "description": "List Markdown headings and generated anchors in source order.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "path": _string_schema(max_length=4096),
                "max_level": {
                    "type": "integer",
                    "minimum": 1,
                    "maximum": 6,
                    "default": 3,
                },
            },
            "required": ["path"],
            "additionalProperties": False,
        },
        "outputSchema": {
            "type": "object",
            "properties": {
                "has_front_matter": {"type": "boolean"},
                "sections": {
                    "type": "array",
                    "items": {
                        "type": "object",
                        "properties": {
                            "level": {"type": "integer"},
                            "title": {"type": "string"},
                            "anchor": {"type": "string"},
                        },
                        "required": ["level", "title", "anchor"],
                        "additionalProperties": False,
                    },
                },
            },
            "required": ["has_front_matter", "sections"],
            "additionalProperties": False,
        },
    },
    {
        "name": "overwrite_section",
        "description": "Replace one existing section body while preserving its heading.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "path": _string_schema(max_length=4096),
                "body": _string_schema(max_length=MAX_MARKDOWN_BYTES),
            },
            "required": ["path", "body"],
            "additionalProperties": False,
        },
    },
    {
        "name": "append_section",
        "description": "Append a level-1 section or one child beneath an existing section.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "path": _string_schema(max_length=4096),
                "title": _string_schema(max_length=MAX_TITLE_CHARS),
                "body": _string_schema(max_length=MAX_MARKDOWN_BYTES),
            },
            "required": ["path", "title", "body"],
            "additionalProperties": False,
        },
    },
    {
        "name": "set_front_matter",
        "description": "Add, replace, or remove leading ---/=== front matter.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "path": _string_schema(max_length=4096),
                "body": _string_schema(max_length=MAX_MARKDOWN_BYTES),
            },
            "required": ["path", "body"],
            "additionalProperties": False,
        },
    },
    {
        "name": "delete_section",
        "description": "Delete one heading section and all of its descendants.",
        "inputSchema": {
            "type": "object",
            "properties": {"path": _string_schema(max_length=4096)},
            "required": ["path"],
            "additionalProperties": False,
        },
    },
)

READ_TOOLS = frozenset({"read_markdown", "list_sections"})
WRITE_TOOLS = frozenset(
    {
        "overwrite_section",
        "append_section",
        "set_front_matter",
        "delete_section",
    }
)
KNOWN_TOOLS = READ_TOOLS | WRITE_TOOLS
_REQUIRED = {
    "read_markdown": {"path"},
    "list_sections": {"path"},
    "overwrite_section": {"path", "body"},
    "append_section": {"path", "title", "body"},
    "set_front_matter": {"path", "body"},
    "delete_section": {"path"},
}
_OPTIONAL = {"list_sections": {"max_level"}}


class ToolArgumentError(Exception):
    """A stable tool argument validation error."""


def _configure_stdio() -> None:
    """Force strict UTF-8 independently of the inherited host locale."""

    for stream, newline in (
        (sys.stdin, None),
        (sys.stdout, "\n"),
        (sys.stderr, "\n"),
    ):
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure is not None:
            reconfigure(encoding="utf-8", errors="strict", newline=newline)


def build_tools(settings: Settings) -> list[dict[str, Any]]:
    enabled = set(READ_TOOLS)
    if settings.writable:
        enabled.update(WRITE_TOOLS)
    return [tool for tool in TOOLS if tool["name"] in enabled]


def _validate_arguments(name: str, arguments: dict[str, Any]) -> None:
    required = _REQUIRED[name]
    allowed = required | _OPTIONAL.get(name, set())
    extras = sorted(set(arguments) - allowed)
    if extras:
        raise ToolArgumentError(f"Unexpected argument: {extras[0]}")
    missing = sorted(required - set(arguments))
    if missing:
        raise ToolArgumentError(f"Missing argument: {missing[0]}")

    for field in required:
        if not isinstance(arguments[field], str):
            raise ToolArgumentError(f"{field} must be a string")
    path = arguments.get("path")
    if isinstance(path, str) and len(path) > 4096:
        raise ToolArgumentError("path is too long")
    title = arguments.get("title")
    if isinstance(title, str) and len(title) > MAX_TITLE_CHARS:
        raise ToolArgumentError("title is too long")
    max_level = arguments.get("max_level", 3)
    if name == "list_sections" and (
        isinstance(max_level, bool)
        or not isinstance(max_level, int)
        or not 1 <= max_level <= 6
    ):
        raise ToolArgumentError("max_level must be an integer from 1 through 6")


class MCPServer:
    def __init__(
        self, filesystem: MarkdownFilesystem, settings: Settings | None = None
    ) -> None:
        self.fs = filesystem
        self.settings = settings or filesystem.settings
        self.tools = build_tools(self.settings)
        self.enabled_tools = {tool["name"] for tool in self.tools}

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
    def _tool_result(
        text: str,
        *,
        is_error: bool = False,
        structured: dict[str, object] | None = None,
    ) -> dict[str, Any]:
        result: dict[str, Any] = {"content": [{"type": "text", "text": text}]}
        if structured is not None:
            result["structuredContent"] = structured
        if is_error:
            result["isError"] = True
        return result

    def handle(self, message: dict[str, Any]) -> dict[str, Any] | None:
        request_id = message.get("id")
        if "id" not in message:
            return None
        method = message.get("method")
        params = message.get("params", {})
        if message.get("jsonrpc") != "2.0" or not isinstance(method, str):
            return self._error(request_id, -32600, "Invalid Request")
        if params is None:
            params = {}
        if not isinstance(params, dict):
            return self._error(request_id, -32602, "Invalid params")

        if method == "initialize":
            requested = params.get("protocolVersion")
            protocol = (
                requested if requested in SUPPORTED_PROTOCOLS else LATEST_PROTOCOL
            )
            return self._result(
                request_id,
                {
                    "protocolVersion": protocol,
                    "capabilities": {"tools": {}},
                    "serverInfo": {
                        "name": "markdown-mcp",
                        "version": __version__,
                        "description": "Root-confined Markdown section tools",
                    },
                },
            )
        if method == "ping":
            return self._result(request_id, {})
        if method == "tools/list":
            return self._result(request_id, {"tools": self.tools})
        if method == "tools/call":
            return self._call_tool(request_id, params)
        return self._error(request_id, -32601, "Method not found")

    def _call_tool(
        self, request_id: Any, params: dict[str, Any]
    ) -> dict[str, Any]:
        name = params.get("name")
        arguments = params.get("arguments", {})
        if not isinstance(name, str):
            return self._error(request_id, -32602, "Invalid tool name")
        if not isinstance(arguments, dict):
            return self._error(request_id, -32602, "Invalid tool arguments")
        if name in KNOWN_TOOLS and name not in self.enabled_tools:
            return self._result(
                request_id,
                self._tool_result(
                    "Tool is unavailable in read-only mode", is_error=True
                ),
            )
        if name not in KNOWN_TOOLS:
            return self._error(request_id, -32602, "Unknown tool")

        try:
            _validate_arguments(name, arguments)
            if name == "read_markdown":
                output = self.fs.read_markdown(arguments["path"])
                result = self._tool_result(output)
            elif name == "list_sections":
                structured = self.fs.list_sections(
                    arguments["path"], arguments.get("max_level", 3)
                )
                text = json.dumps(
                    structured, ensure_ascii=False, separators=(",", ":")
                )
                result = self._tool_result(text, structured=structured)
            elif name == "overwrite_section":
                output = self.fs.overwrite_section(
                    arguments["path"], arguments["body"]
                )
                result = self._tool_result(output)
            elif name == "append_section":
                output = self.fs.append_section(
                    arguments["path"], arguments["title"], arguments["body"]
                )
                result = self._tool_result(output)
            elif name == "set_front_matter":
                output = self.fs.set_front_matter(
                    arguments["path"], arguments["body"]
                )
                result = self._tool_result(output)
            else:
                output = self.fs.delete_section(arguments["path"])
                result = self._tool_result(output)
            return self._result(request_id, result)
        except (FileAccessError, ToolArgumentError, TypeError) as exc:
            return self._result(
                request_id, self._tool_result(str(exc), is_error=True)
            )


def run(settings: Settings | str) -> None:
    _configure_stdio()
    if not isinstance(settings, Settings):
        settings = Settings.for_root(settings)
    server = MCPServer(MarkdownFilesystem(settings), settings)
    for line in sys.stdin:
        try:
            message = json.loads(line)
            if not isinstance(message, dict):
                response = MCPServer._error(None, -32600, "Invalid Request")
            else:
                response = server.handle(message)
        except json.JSONDecodeError:
            response = MCPServer._error(None, -32700, "Parse error")
        except Exception as exc:  # Keep serving after an unexpected request.
            detail = f"{type(exc).__name__}: {exc}"[:1000]
            print(f"markdown-mcp: {detail}", file=sys.stderr)
            response = MCPServer._error(None, -32603, "Internal error")
        if response is not None:
            print(
                json.dumps(
                    response, ensure_ascii=False, separators=(",", ":")
                ),
                flush=True,
            )


def main(argv: Sequence[str] | None = None) -> None:
    _configure_stdio()
    settings = load_settings(argv)
    run(settings)


if __name__ == "__main__":
    main()
