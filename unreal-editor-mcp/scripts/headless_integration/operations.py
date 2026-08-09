"""Lost-response reconciliation and bounded retry-when-ready support."""

from __future__ import annotations

import http.client
import json
import time

from unreal_editor_mcp import __version__
from unreal_editor_mcp.bridge import BRIDGE_PATH, UnrealBridge
from unreal_editor_mcp.discovery import read_discovery, read_token
from unreal_editor_mcp.errors import BridgeError, ErrorCode
from unreal_editor_mcp.project import ProjectLayout


def send_without_reading(layout: ProjectLayout, command: str, arguments: dict[str, object]) -> None:
    record = read_discovery(layout)
    connection = http.client.HTTPConnection("127.0.0.1", record.port, timeout=2.0)
    connection.request("POST", BRIDGE_PATH, body=json.dumps({"command": command, "arguments": arguments}, separators=(",", ":")).encode(), headers={"Authorization": "Bearer " + read_token(layout), "Content-Type": "application/json", "X-Unreal-MCP-Version": __version__})
    connection.close()


def reconcile_operation(bridge: UnrealBridge, operation_id: str, bridge_instance_id: str) -> dict[str, object]:
    deadline = time.monotonic() + 10.0
    while time.monotonic() < deadline:
        status = bridge.call("operation_status", {"operation_id": operation_id, "bridge_instance_id": bridge_instance_id})
        if status.get("state") in {"committed", "partial", "rejected", "cancelled", "outcome_unknown"}:
            return status
        time.sleep(0.05)
    raise TimeoutError("lost mutation response did not reach a retained terminal state")


def call_when_ready(bridge: UnrealBridge, command: str, arguments: dict[str, object], *, attempts: int = 100) -> dict[str, object]:
    if type(attempts) is not int or not 1 <= attempts <= 100:
        raise ValueError("attempts must be an integer from 1 to 100")
    for attempt in range(attempts):
        try:
            return bridge.call(command, arguments)
        except BridgeError as error:
            if error.code != ErrorCode.EDITOR_UNAVAILABLE or attempt + 1 == attempts:
                raise
            time.sleep(0.1)
    raise AssertionError("unreachable retry state")
