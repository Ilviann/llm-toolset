"""Bounded newline-delimited JSON-RPC transport for stdio MCP hosts."""

from __future__ import annotations

import json
import sys
from typing import Any, Protocol, TextIO


MAX_MCP_MESSAGE_CHARS = 1024 * 1024


class RequestHandler(Protocol):
    def handle(self, message: dict[str, Any]) -> dict[str, Any] | None: ...


def result(request_id: Any, value: dict[str, Any]) -> dict[str, Any]:
    return {"jsonrpc": "2.0", "id": request_id, "result": value}


def error(request_id: Any, code: int, message: str) -> dict[str, Any]:
    return {"jsonrpc": "2.0", "id": request_id, "error": {"code": code, "message": message[:512]}}


def tool_result(value: Any, *, is_error: bool = False) -> dict[str, Any]:
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
    source = sys.stdin if input_stream is None else input_stream
    destination = sys.stdout if output_stream is None else output_stream
    diagnostics = sys.stderr if error_stream is None else error_stream
    try:
        while True:
            line = source.readline(MAX_MCP_MESSAGE_CHARS + 2)
            if line == "":
                break
            if len(line) > MAX_MCP_MESSAGE_CHARS and not line.endswith("\n"):
                while True:
                    remainder = source.readline(MAX_MCP_MESSAGE_CHARS + 2)
                    if remainder == "" or remainder.endswith("\n"):
                        break
                response = error(None, -32700, "MCP message exceeds the size limit")
            else:
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
                    print(f"unreal-editor-mcp: {exc}", file=diagnostics)
                    response = error(None, -32603, "Internal error")
            if response is not None:
                print(json.dumps(response, ensure_ascii=False, separators=(",", ":")), file=destination, flush=True)
    finally:
        close = getattr(server, "close", None)
        if callable(close):
            close()
