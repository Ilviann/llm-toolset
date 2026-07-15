"""Newline-delimited JSON-RPC transport for stdio MCP hosts."""

from __future__ import annotations

import base64
import json
import sys
from dataclasses import dataclass
from typing import Any, Protocol, TextIO


@dataclass(frozen=True)
class ToolImageResult:
    """Validated image bytes plus concise metadata for an MCP tool result."""

    data: bytes
    metadata: dict[str, Any]
    mime_type: str = "image/png"


class RequestHandler(Protocol):
    def handle(self, message: dict[str, Any]) -> dict[str, Any] | None: ...


class ClosableRequestHandler(RequestHandler, Protocol):
    def close(self) -> None: ...


def result(request_id: Any, value: dict[str, Any]) -> dict[str, Any]:
    return {"jsonrpc": "2.0", "id": request_id, "result": value}


def error(request_id: Any, code: int, message: str) -> dict[str, Any]:
    return {
        "jsonrpc": "2.0",
        "id": request_id,
        "error": {"code": code, "message": message},
    }


def tool_result(value: Any, *, is_error: bool = False) -> dict[str, Any]:
    if isinstance(value, ToolImageResult):
        return {
            "content": [
                {
                    "type": "text",
                    "text": json.dumps(
                        value.metadata,
                        ensure_ascii=False,
                        separators=(",", ":"),
                        sort_keys=True,
                    ),
                },
                {
                    "type": "image",
                    "data": base64.b64encode(value.data).decode("ascii"),
                    "mimeType": value.mime_type,
                },
            ]
        }
    text = value if isinstance(value, str) else json.dumps(
        value, ensure_ascii=False, separators=(",", ":"), sort_keys=True
    )
    response: dict[str, Any] = {"content": [{"type": "text", "text": text}]}
    if is_error:
        response["isError"] = True
    return response


def serve(
    server: RequestHandler,
    *,
    input_stream: TextIO | None = None,
    output_stream: TextIO | None = None,
    error_stream: TextIO | None = None,
) -> None:
    """Serve one JSON-RPC object per line without leaking diagnostics to stdout."""
    source = sys.stdin if input_stream is None else input_stream
    destination = sys.stdout if output_stream is None else output_stream
    diagnostics = sys.stderr if error_stream is None else error_stream
    try:
        for line in source:
            try:
                message = json.loads(line)
                response = (
                    server.handle(message)
                    if isinstance(message, dict)
                    else error(None, -32600, "Invalid Request")
                )
            except json.JSONDecodeError:
                response = error(None, -32700, "Parse error")
            except Exception as exc:
                print(f"godot-editor-mcp: {exc}", file=diagnostics)
                response = error(None, -32603, "Internal error")
            if response is not None:
                print(
                    json.dumps(response, ensure_ascii=False, separators=(",", ":")),
                    file=destination,
                    flush=True,
                )
    finally:
        close = getattr(server, "close", None)
        if callable(close):
            close()
