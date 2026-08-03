"""Readonly MCP catalog and project-content preservation acceptance."""

from __future__ import annotations

import hashlib
import json
import uuid
from pathlib import Path
from typing import Any

from unreal_editor_mcp.project import ProjectLayout
from unreal_editor_mcp.server import MCPServer


READONLY_TOOL_NAMES = [
    "capabilities",
    "editor_state",
    "operation_status",
    "asset_references",
    "level_inspect",
    "level_open",
    "blueprint_inspect",
    "blueprint_action_catalog",
    "game_data_inspect",
]

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


def _continue_once(server: MCPServer, name: str, result: dict[str, Any]) -> None:
    cursor = result.get("next_cursor")
    if isinstance(cursor, str):
        _call(server, name, {"cursor": cursor, "page_size": 1})


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
    server = MCPServer(bridge, project_identity=layout.identity())
    listed = server.handle({"jsonrpc": "2.0", "id": 1, "method": "tools/list"})
    names = [tool["name"] for tool in listed["result"]["tools"]]
    if names != READONLY_TOOL_NAMES:
        raise AssertionError(f"readonly tool catalog mismatch: {names!r}")

    before = _project_fingerprint(layout.root)
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

    blueprint = _call(server, "blueprint_inspect", {
        "mode": "inspect",
        "asset_path": blueprint_path,
        "sections": ["summary", "graphs"],
        "page_size": 1,
    })
    _continue_once(server, "blueprint_inspect", blueprint)
    graph = next(
        (record for record in blueprint.get("records", []) if record.get("section") == "graph"),
        None,
    )
    if not isinstance(graph, dict):
        graph_page = _call(server, "blueprint_inspect", {
            "mode": "inspect",
            "asset_path": blueprint_path,
            "sections": ["graphs"],
            "page_size": 100,
        })
        graph = next(
            (record for record in graph_page.get("records", []) if record.get("section") == "graph"),
            None,
        )
    if not isinstance(graph, dict):
        raise AssertionError(f"readonly Blueprint graph fixture is unavailable: {blueprint!r}")
    _call(server, "blueprint_action_catalog", {
        "asset_path": blueprint_path,
        "graph_id": graph["id"],
        "expected_snapshot": blueprint["snapshot_id"],
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
