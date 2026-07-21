"""Bounded authenticated HTTP client for the Unreal editor plugin."""

from __future__ import annotations

import http.client
import json
import threading
from collections.abc import Callable
from typing import Any

from . import __version__
from .discovery import DiscoveryRecord, read_discovery, read_token
from .errors import BridgeError, ErrorCode, bridge_error_from_payload
from .project import ProjectLayout


BRIDGE_PATH = "/unreal-mcp/v1/command"
MAX_REQUEST_BYTES = 64 * 1024
MAX_RESPONSE_BYTES = 256 * 1024
MUTATING_COMMANDS = {
    "blueprint_create", "blueprint_compile", "blueprint_save",
    "blueprint_component_edit", "blueprint_default_edit", "blueprint_member_edit",
}


class UnrealBridge:
    def __init__(
        self,
        layout: ProjectLayout,
        *,
        port: int | None = None,
        timeout: float = 3.0,
        connection_factory: Callable[..., http.client.HTTPConnection] = http.client.HTTPConnection,
    ) -> None:
        if port is not None and (type(port) is not int or not 1 <= port <= 65535):
            raise BridgeError("Port must be between 1 and 65535", code=ErrorCode.INVALID_CONFIGURATION)
        if not 0.05 <= timeout <= 30.0:
            raise BridgeError("Timeout must be between 0.05 and 30 seconds", code=ErrorCode.INVALID_CONFIGURATION)
        self.layout = layout
        self.port = port
        self.timeout = timeout
        self._connection_factory = connection_factory
        self._lock = threading.Lock()
        self._active: set[http.client.HTTPConnection] = set()
        self._closed = False
        self._bridge_instance_id = ""

    def call(self, command: str, arguments: dict[str, Any] | None = None) -> Any:
        if command not in {
            "capabilities",
            "editor_state",
            "operation_status",
            "blueprint_inspect",
            "blueprint_action_catalog",
            "blueprint_create",
            "blueprint_compile",
            "blueprint_save",
            "blueprint_component_edit",
            "blueprint_default_edit",
            "blueprint_member_edit",
        }:
            raise BridgeError("Unsupported bridge command", code=ErrorCode.INVALID_ARGUMENT)
        record = read_discovery(self.layout)
        if self.port is not None and self.port != record.port:
            raise BridgeError(
                "Configured port does not match the active Unreal bridge",
                code=ErrorCode.INVALID_CONFIGURATION,
                details={"configured_port": self.port, "discovered_port": record.port},
            )
        if command != "capabilities" and record.bridge_version != __version__:
            raise BridgeError(
                "Python server and Unreal plugin versions do not match",
                code=ErrorCode.VERSION_MISMATCH,
                details={"python_version": __version__, "bridge_version": record.bridge_version},
            )
        token = read_token(self.layout)
        request = json.dumps(
            {"command": command, "arguments": arguments or {}},
            ensure_ascii=False,
            separators=(",", ":"),
        ).encode("utf-8")
        if len(request) > MAX_REQUEST_BYTES:
            raise BridgeError("Request is too large", code=ErrorCode.REQUEST_TOO_LARGE)
        connection = self._connection_factory("127.0.0.1", record.port, timeout=self.timeout)
        with self._lock:
            if self._closed:
                raise BridgeError("Bridge client is closed", code=ErrorCode.CANCELLED)
            self._active.add(connection)
        try:
            connection.request(
                "POST",
                BRIDGE_PATH,
                body=request,
                headers={
                    "Authorization": f"Bearer {token}",
                    "Content-Type": "application/json",
                    "X-Unreal-MCP-Version": __version__,
                },
            )
            response = connection.getresponse()
            body = response.read(MAX_RESPONSE_BYTES + 1)
        except TimeoutError:
            if command in MUTATING_COMMANDS:
                raise BridgeError(
                    "Mutation response was lost; resolve operation_status before retrying",
                    code=ErrorCode.OUTCOME_UNKNOWN,
                    details={
                        "operation_id": (arguments or {}).get("operation_id", ""),
                        "bridge_instance_id": self._bridge_instance_id,
                    },
                    retryable=False,
                ) from None
            raise BridgeError("Unreal bridge request timed out", code=ErrorCode.TIMEOUT, retryable=True) from None
        except (OSError, http.client.HTTPException):
            with self._lock:
                closed = self._closed
            raise BridgeError(
                "Unreal bridge request was cancelled" if closed else "Unreal Editor is unavailable",
                code=ErrorCode.CANCELLED if closed else ErrorCode.EDITOR_UNAVAILABLE,
                retryable=not closed,
            ) from None
        finally:
            connection.close()
            with self._lock:
                self._active.discard(connection)
        if len(body) > MAX_RESPONSE_BYTES:
            raise BridgeError("Unreal bridge response is too large", code=ErrorCode.RESPONSE_TOO_LARGE)
        result = self._decode(response.status, body, record)
        if isinstance(result, dict):
            instance_id = result.get("bridge_instance_id")
            if isinstance(instance_id, str) and len(instance_id) == 32:
                self._bridge_instance_id = instance_id
        return result

    @staticmethod
    def _decode(status: int, body: bytes, record: DiscoveryRecord) -> Any:
        try:
            message = json.loads(body)
        except (UnicodeDecodeError, json.JSONDecodeError):
            raise BridgeError("Unreal Editor returned invalid JSON", code=ErrorCode.INVALID_RESPONSE) from None
        if not isinstance(message, dict) or set(message) - {"ok", "result", "error"} or not isinstance(message.get("ok"), bool):
            raise BridgeError("Unreal Editor returned an invalid response", code=ErrorCode.INVALID_RESPONSE)
        if message["ok"] is False:
            raise bridge_error_from_payload(message.get("error"))
        if status != 200 or "result" not in message or "error" in message:
            raise BridgeError("Unreal Editor returned an invalid response", code=ErrorCode.INVALID_RESPONSE)
        result = message["result"]
        if isinstance(result, dict) and result.get("project_hash") not in {None, record.project_hash}:
            raise BridgeError("Unreal bridge project identity changed", code=ErrorCode.INVALID_RESPONSE)
        return result

    def close(self) -> None:
        with self._lock:
            self._closed = True
            active = list(self._active)
        for connection in active:
            connection.close()
