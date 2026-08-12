"""Readonly MCP catalog and project-content preservation acceptance."""

from __future__ import annotations

import hashlib
import json
import uuid
from pathlib import Path
from typing import Any

from unreal_editor_mcp.project import ProjectLayout
from unreal_editor_mcp.server import MCPServer
from unreal_editor_mcp.tool_catalog import WRITABLE_TOOL_NAMES


READONLY_TOOL_NAMES = [
    "capabilities",
    "editor_state",
    "operation_status",
    "asset_inspect",
    "asset_references",
    "level_inspect",
    "level_open",
    "blueprint_action_catalog",
    "game_data_inspect",
]
READONLY_LIFECYCLE_TOOL_NAMES = [*READONLY_TOOL_NAMES, "editor_lifecycle"]

_GENERATED_DIRECTORIES = {
    ".git",
    ".vs",
    "binaries",
    "deriveddatacache",
    "intermediate",
    "saved",
}


def _project_fingerprint(root: Path) -> dict[str, tuple[int, str]]:
    """Hash project-owned files while excluding generated/runtime state."""
    fingerprint: dict[str, tuple[int, str]] = {}
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root)
        if any(part.casefold() in _GENERATED_DIRECTORIES for part in relative.parts):
            continue
        if path.is_symlink():
            fingerprint[relative.as_posix()] = (-1, str(path.readlink()))
            continue
        if not path.is_file():
            continue
        digest = hashlib.sha256()
        size = 0
        with path.open("rb") as stream:
            while chunk := stream.read(1024 * 1024):
                size += len(chunk)
                digest.update(chunk)
        fingerprint[relative.as_posix()] = (size, digest.hexdigest())
    return fingerprint


def _call(server: MCPServer, name: str, arguments: dict[str, Any]) -> dict[str, Any]:
    response = server.handle({
        "jsonrpc": "2.0",
        "id": uuid.uuid4().hex,
        "method": "tools/call",
        "params": {"name": name, "arguments": arguments},
    })
    if not isinstance(response, dict) or "error" in response:
        raise AssertionError(f"readonly MCP call failed before dispatch: {name}: {response!r}")
    tool_result = response.get("result")
    if not isinstance(tool_result, dict) or tool_result.get("isError") is True:
        raise AssertionError(f"readonly MCP tool returned an error: {name}: {tool_result!r}")
    content = tool_result.get("content", [])
    if len(content) != 1 or content[0].get("type") != "text":
        raise AssertionError(f"readonly MCP result shape is invalid: {name}: {tool_result!r}")
    payload = json.loads(content[0]["text"])
    if not isinstance(payload, dict):
        raise AssertionError(f"readonly MCP payload is not an object: {name}: {payload!r}")
    return payload


def _call_text(server: MCPServer, name: str, arguments: dict[str, Any]) -> str:
    response = server.handle({
        "jsonrpc": "2.0", "id": uuid.uuid4().hex, "method": "tools/call",
        "params": {"name": name, "arguments": arguments},
    })
    if not isinstance(response, dict) or "error" in response:
        raise AssertionError(f"readonly MCP call failed before dispatch: {name}: {response!r}")
    tool_result = response.get("result")
    if not isinstance(tool_result, dict) or tool_result.get("isError") is True:
        raise AssertionError(f"readonly MCP tool returned an error: {name}: {tool_result!r}")
    content = tool_result.get("content", [])
    if len(content) != 1 or content[0].get("type") != "text" or not isinstance(content[0].get("text"), str):
        raise AssertionError(f"readonly MCP result shape is invalid: {name}: {tool_result!r}")
    return content[0]["text"]


def _yaml_string_field(document: str, field: str) -> str:
    prefix = f'"{field}": '
    matches = [line.strip()[len(prefix):] for line in document.splitlines() if line.strip().startswith(prefix)]
    if len(matches) != 1:
        raise AssertionError(f"asset_inspect YAML field is missing or ambiguous: {field}: {document!r}")
    value = json.loads(matches[0])
    if not isinstance(value, str):
        raise AssertionError(f"asset_inspect YAML field is not a string: {field}")
    return value


def _continue_once(server: MCPServer, name: str, result: dict[str, Any]) -> None:
    cursor = result.get("next_cursor")
    if isinstance(cursor, str):
        _call(server, name, {"cursor": cursor, "page_size": 1})


class _RecordingBridge:
    """Record native dispatch while retaining the production bridge behavior."""

    def __init__(self, bridge: Any) -> None:
        self.bridge = bridge
        self.calls: list[str] = []

    def call(self, command: str, arguments: dict[str, Any] | None = None) -> Any:
        self.calls.append(command)
        return self.bridge.call(command, arguments)

    def close(self) -> None:
        self.bridge.close()


def verify_readonly_lifecycle_server(
    server: MCPServer,
    recording_bridge: _RecordingBridge,
    layout: ProjectLayout,
) -> None:
    """Prove lifecycle-only access and preservation through a real restart."""
    before = _project_fingerprint(layout.root)
    listed = server.handle({"jsonrpc": "2.0", "id": 1, "method": "tools/list"})
    names = [tool["name"] for tool in listed["result"]["tools"]]
    if names != READONLY_LIFECYCLE_TOOL_NAMES:
        raise AssertionError(f"readonly+lifecycle tool catalog mismatch: {names!r}")

    before_rejection = list(recording_bridge.calls)
    for name in sorted(WRITABLE_TOOL_NAMES):
        rejected = server.handle({
            "jsonrpc": "2.0",
            "id": uuid.uuid4().hex,
            "method": "tools/call",
            "params": {"name": name, "arguments": {}},
        })
        error = rejected.get("error", {}) if isinstance(rejected, dict) else {}
        if error.get("code") != -32602 or error.get("message") != "Unknown tool":
            raise AssertionError(f"writable tool was not rejected as unknown: {name}: {rejected!r}")
    if recording_bridge.calls != before_rejection:
        raise AssertionError("writable-tool rejection contacted the native bridge")

    launched = False
    stopped = False
    try:
        launch = _call(server, "editor_lifecycle", {
            "operation_id": uuid.uuid4().hex,
            "operation": "launch",
        })
        if launch.get("state") != "ready":
            raise AssertionError(f"lifecycle acceptance did not launch a new editor: {launch!r}")
        launched = True
        launched_instance = launch.get("new_bridge_instance_id")
        if not isinstance(launched_instance, str) or len(launched_instance) != 32:
            raise AssertionError(f"launched bridge identity is invalid: {launch!r}")

        capabilities = _call(server, "capabilities", {})
        if capabilities.get("access_mode") != "readonly" \
                or capabilities.get("editor_lifecycle", {}).get("enabled") is not True:
            raise AssertionError(f"lifecycle-only access dimensions changed: {capabilities!r}")

        restart = _call(server, "editor_lifecycle", {
            "operation_id": uuid.uuid4().hex,
            "operation": "restart",
        })
        restarted_instance = restart.get("new_bridge_instance_id")
        if restart.get("state") != "ready" \
                or restart.get("old_bridge_instance_id") != launched_instance \
                or not isinstance(restarted_instance, str) \
                or len(restarted_instance) != 32 \
                or restarted_instance == launched_instance:
            raise AssertionError(f"restart did not replace the bridge instance: {restart!r}")

        shutdown = _call(server, "editor_lifecycle", {
            "operation_id": uuid.uuid4().hex,
            "operation": "shutdown",
        })
        if shutdown.get("state") != "stopped":
            raise AssertionError(f"lifecycle acceptance did not stop the editor: {shutdown!r}")
        stopped = True

        after = _project_fingerprint(layout.root)
        if after != before:
            changed = sorted(set(before) ^ set(after) | {
                path for path in set(before) & set(after) if before[path] != after[path]
            })
            raise AssertionError(
                f"readonly lifecycle changed project-owned files: {changed[:32]!r}"
            )
    finally:
        if launched and not stopped:
            try:
                _call(server, "editor_lifecycle", {
                    "operation_id": uuid.uuid4().hex,
                    "operation": "shutdown",
                })
            except Exception:
                pass


def verify_windows_readonly_lifecycle(
    layout: ProjectLayout,
    editor_executable: Path,
    *,
    startup_timeout: float = 120.0,
) -> None:
    """Run the production lifecycle-only MCP server against UnrealEditor.exe."""
    from unreal_editor_mcp.bridge import UnrealBridge
    from unreal_editor_mcp.lifecycle import EditorLifecycle

    recording_bridge = _RecordingBridge(UnrealBridge(layout, timeout=3.0))
    lifecycle = EditorLifecycle(
        layout,
        recording_bridge,
        editor_executable=editor_executable,
        startup_timeout=startup_timeout,
    )
    server = MCPServer(
        recording_bridge,
        project_identity=layout.identity(),
        lifecycle=lifecycle,
    )
    try:
        verify_readonly_lifecycle_server(server, recording_bridge, layout)
    finally:
        server.close()


def verify_readonly_mode(
    bridge: Any,
    layout: ProjectLayout,
    *,
    bridge_instance_id: str,
    blueprint_path: str,
    game_data_path: str,
    map_path: str,
) -> None:
    """Exercise every readonly tool and prove project-owned bytes are unchanged."""
    before = _project_fingerprint(layout.root)
    server = MCPServer(bridge, project_identity=layout.identity())
    listed = server.handle({"jsonrpc": "2.0", "id": 1, "method": "tools/list"})
    names = [tool["name"] for tool in listed["result"]["tools"]]
    if names != READONLY_TOOL_NAMES:
        raise AssertionError(f"readonly tool catalog mismatch: {names!r}")

    capabilities = _call(server, "capabilities", {})
    if capabilities.get("access_mode") != "readonly":
        raise AssertionError(f"readonly capability mode mismatch: {capabilities!r}")
    _call(server, "editor_state", {})
    _call(server, "operation_status", {
        "operation_id": uuid.uuid4().hex,
        "bridge_instance_id": bridge_instance_id,
    })

    references = _call(server, "asset_references", {
        "asset_path": game_data_path,
        "page_size": 1,
    })
    _continue_once(server, "asset_references", references)

    maps = _call(server, "level_inspect", {"mode": "discover", "page_size": 1})
    _continue_once(server, "level_inspect", maps)
    _call(server, "level_inspect", {"mode": "current"})
    _call(server, "level_open", {
        "operation_id": uuid.uuid4().hex,
        "map_path": map_path,
    })

    inspected = _call_text(server, "asset_inspect", {
        "asset_path": blueprint_path,
        "selector": "event_graphs/EventGraph",
        "verbose": True,
    })
    _call(server, "blueprint_action_catalog", {
        "asset_path": blueprint_path,
        "graph_id": _yaml_string_field(inspected, "graph_guid"),
        "expected_snapshot": _yaml_string_field(inspected, "snapshot_id"),
        "limit": 1,
    })

    game_data = _call(server, "game_data_inspect", {
        "target": "data_table",
        "asset_path": game_data_path,
        "page_size": 1,
    })
    _continue_once(server, "game_data_inspect", game_data)

    after = _project_fingerprint(layout.root)
    if after != before:
        changed = sorted(set(before) ^ set(after) | {
            path for path in set(before) & set(after) if before[path] != after[path]
        })
        raise AssertionError(f"readonly tools changed project-owned files: {changed[:32]!r}")
