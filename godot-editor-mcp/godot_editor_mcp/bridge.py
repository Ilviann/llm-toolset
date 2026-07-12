"""Bounded localhost client for the Godot editor plugin."""

from __future__ import annotations

import json
import socket
from pathlib import Path
from typing import Any


MAX_REQUEST_BYTES = 64 * 1024
MAX_RESPONSE_BYTES = 256 * 1024


class BridgeError(Exception):
    """A concise error safe to return to an MCP client."""


class GodotBridge:
    def __init__(
        self,
        project: str | Path,
        *,
        host: str = "127.0.0.1",
        port: int = 6505,
        timeout: float = 3.0,
    ) -> None:
        root = Path(project).expanduser().resolve(strict=True)
        if not root.is_dir() or not (root / "project.godot").is_file():
            raise BridgeError("Project must be a folder containing project.godot")
        if host not in {"127.0.0.1", "::1", "localhost"}:
            raise BridgeError("Bridge host must be localhost")
        if not isinstance(port, int) or not 1 <= port <= 65535:
            raise BridgeError("Port must be between 1 and 65535")
        self.project = root
        self.host = host
        self.port = port
        self.timeout = timeout

    def _token(self) -> str:
        path = self.project / ".godot" / "godot_mcp_token"
        try:
            token = path.read_text(encoding="ascii").strip()
        except OSError:
            raise BridgeError(
                "Godot bridge token not found; enable the Godot MCP editor plugin"
            ) from None
        if len(token) != 64 or any(c not in "0123456789abcdef" for c in token):
            raise BridgeError("Godot bridge token is invalid; restart the editor plugin")
        return token

    def call(self, command: str, arguments: dict[str, Any] | None = None) -> Any:
        request = {
            "token": self._token(),
            "command": command,
            "arguments": arguments or {},
        }
        encoded = json.dumps(
            request, ensure_ascii=False, separators=(",", ":")
        ).encode("utf-8") + b"\n"
        if len(encoded) > MAX_REQUEST_BYTES:
            raise BridgeError("Request is too large")

        try:
            with socket.create_connection((self.host, self.port), self.timeout) as peer:
                peer.settimeout(self.timeout)
                peer.sendall(encoded)
                response = self._read_line(peer)
        except (OSError, TimeoutError):
            raise BridgeError(
                "Godot editor is unavailable; open the project and enable the plugin"
            ) from None

        try:
            message = json.loads(response)
        except (UnicodeDecodeError, json.JSONDecodeError):
            raise BridgeError("Godot editor returned an invalid response") from None
        if not isinstance(message, dict) or not isinstance(message.get("ok"), bool):
            raise BridgeError("Godot editor returned an invalid response")
        if not message["ok"]:
            error = message.get("error")
            raise BridgeError(error if isinstance(error, str) else "Godot command failed")
        return message.get("result")

    @staticmethod
    def _read_line(peer: socket.socket) -> str:
        data = bytearray()
        while len(data) <= MAX_RESPONSE_BYTES:
            chunk = peer.recv(min(8192, MAX_RESPONSE_BYTES + 1 - len(data)))
            if not chunk:
                break
            data.extend(chunk)
            newline = data.find(b"\n")
            if newline >= 0:
                return bytes(data[:newline]).decode("utf-8")
        if len(data) > MAX_RESPONSE_BYTES:
            raise BridgeError("Godot editor response is too large")
        raise BridgeError("Godot editor closed the connection without a response")
