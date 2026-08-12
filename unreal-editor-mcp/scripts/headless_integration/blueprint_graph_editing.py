"""Author a representative Blueprint through semantic inspection and mutation."""

from __future__ import annotations

import os
import uuid

from unreal_editor_mcp.bridge import UnrealBridge
from unreal_editor_mcp.project import ProjectLayout

from .blueprint_declarations import author_blueprint_declarations


def author_blueprint_scenario(
    bridge: UnrealBridge,
    layout: ProjectLayout,
    capabilities: dict[str, object],
    created: dict[str, object],
    phase_fourteen_families: dict[str, dict[str, object]],
    phase_fifteen_game_instance: dict[str, object],
) -> dict[str, object]:
    """Author declarations, one graph node, compile/save, and framework assignments."""
    declarations = author_blueprint_declarations(bridge, layout, capabilities, created)
    asset_path = declarations["asset_path"]
    event_graph_id = declarations["event_graph_id"]
    snapshot = declarations["custom_event"]["snapshot_id"]
    catalog = bridge.call("blueprint_action_catalog", {
        "asset_path": asset_path,
        "graph_id": event_graph_id,
        "expected_snapshot": snapshot,
        "text": "Branch",
        "node_family": "flow_control",
        "limit": 50,
    })
    actions = [item for item in catalog.get("actions", [])
               if str(item.get("title", "")).casefold() == "branch"]
    if not actions:
        raise AssertionError(f"representative Branch action is unavailable: {catalog!r}")
    added = bridge.call("blueprint_graph_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": snapshot,
        "operation": "add_node",
        "graph_id": event_graph_id,
        "action_id": actions[0]["action_id"],
        "position": {"x": 480, "y": -160},
    })
    graph_node_id = added.get("changed", {}).get("node", {}).get("id")
    if not isinstance(graph_node_id, str):
        raise AssertionError(f"representative graph mutation omitted its identity: {added!r}")
    compiled = bridge.call("blueprint_compile", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": added["snapshot_id"],
    })
    saved = bridge.call("blueprint_save", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": compiled["snapshot_id"],
    })
    if saved.get("saved") is not True or saved.get("package_dirty") is not False:
        raise AssertionError(f"explicit Blueprint save contract mismatch: {saved!r}")
    if os.name == "posix" and layout.token_file.stat().st_mode & 0o077:
        raise AssertionError("bridge token permissions are broader than the owning user")

    assigned_game_mode_class = phase_fourteen_families["game_mode_base"]["asset_path"] + "_C"
    assigned_game_instance_class = phase_fifteen_game_instance["asset_path"] + "_C"
    bridge.call("gameplay_framework_edit", {
        "operation_id": uuid.uuid4().hex,
        "project_hash": capabilities["project_hash"],
        "setting": "default_game_mode",
        "class_path": assigned_game_mode_class,
        "expected_class": "/Script/Engine.GameModeBase",
    })
    bridge.call("gameplay_framework_edit", {
        "operation_id": uuid.uuid4().hex,
        "project_hash": capabilities["project_hash"],
        "setting": "default_game_instance",
        "class_path": assigned_game_instance_class,
        "expected_class": "/Script/Engine.GameInstance",
    })
    return {
        "assigned_game_instance_class": assigned_game_instance_class,
        "assigned_game_mode_class": assigned_game_mode_class,
        "created_snapshot": saved["snapshot_id"],
        "event_graph_id": event_graph_id,
        "graph_node_id": graph_node_id,
    }
