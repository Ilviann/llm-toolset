"""Verify semantic Blueprint inspection and mutation snapshots after restart."""

from __future__ import annotations

import json

from unreal_editor_mcp.bridge import UnrealBridge
from unreal_editor_mcp.project import ProjectLayout


def _root(bridge: UnrealBridge, asset_path: str) -> dict[str, object]:
    return bridge.call("asset_inspect", {"asset_path": asset_path})


def _event_graph(bridge: UnrealBridge, asset_path: str) -> dict[str, object]:
    return bridge.call("asset_inspect", {
        "asset_path": asset_path,
        "selector": "event_graphs/EventGraph",
        "verbose": True,
    })


def _node_guids(inspection: dict[str, object]) -> set[str]:
    return {
        value
        for node in inspection.get("graph", {}).get("nodes", [])
        if isinstance(node, dict)
        for value in [node.get("debug", {}).get("node_guid")]
        if isinstance(value, str)
    }


def verify_restarted_blueprints(
    reloaded_bridge: UnrealBridge,
    layout: ProjectLayout,
    phase_two_loaded_snapshot: str,
    phase_two_loaded_inspection: dict[str, object],
    phase_fourteen_families: dict[str, dict[str, object]],
    phase_fifteen_game_instance: dict[str, object],
    scenario: dict[str, object],
) -> None:
    """Verify compact roots, exact graph selectors, snapshots, and catalogs after restart."""
    del layout, phase_two_loaded_inspection
    phase_two = _root(
        reloaded_bridge,
        "/Game/UnrealMCPPhase2/BP_InspectionFixture.BP_InspectionFixture",
    )
    if phase_two.get("snapshot_id") != phase_two_loaded_snapshot \
            or phase_two.get("asset", {}).get("type") != "actor_blueprint":
        raise AssertionError(f"Phase 2 semantic snapshot changed after restart: {phase_two!r}")

    asset_path = "/Game/UnrealMCPPhase4/BP_ComponentFixture.BP_ComponentFixture"
    reloaded = _root(reloaded_bridge, asset_path)
    if reloaded.get("snapshot_id") != scenario["created_snapshot"] \
            or reloaded.get("asset", {}).get("type") != "actor_blueprint":
        raise AssertionError(f"created Blueprint semantic root changed after restart: {reloaded!r}")
    if "Health" not in {item.get("name") for item in reloaded.get("variables", [])}:
        raise AssertionError(f"member declaration changed after restart: {reloaded!r}")
    if not {"ComputeHealth", "OnRep_Health"}.issubset(
            {item.get("name") for item in reloaded.get("functions", [])}):
        raise AssertionError(f"function declarations changed after restart: {reloaded!r}")
    if "ClampHealth" not in {item.get("name") for item in reloaded.get("macros", [])}:
        raise AssertionError(f"macro declaration changed after restart: {reloaded!r}")

    graph = _event_graph(reloaded_bridge, asset_path)
    graph_id = graph.get("graph", {}).get("debug", {}).get("graph_guid")
    if graph_id != scenario["event_graph_id"]:
        raise AssertionError(f"event graph identity changed after restart: {graph!r}")
    node_guids = _node_guids(graph)
    if scenario["graph_node_id"] not in node_guids:
        raise AssertionError("event graph semantic body changed after restart")

    catalog_base = {
        "asset_path": asset_path,
        "graph_id": graph_id,
        "expected_snapshot": reloaded["snapshot_id"],
    }
    for family, filters in {
        "variable_get": {"member": "Health", "node_family": "variable_get"},
        "function_call": {"function": "ComputeHealth", "node_family": "function_call"},
        "event": {"node_family": "event"},
        "flow_control": {"node_family": "flow_control"},
        "cast": {"node_family": "cast", "owner_class": "/Script/Engine.Actor"},
        "literal": {"node_family": "literal", "owner_class": "/Script/Engine.KismetSystemLibrary",
                    "function": "MakeLiteralInt"},
        "operator": {"node_family": "operator", "owner_class": "/Script/Engine.KismetMathLibrary"},
    }.items():
        catalog = reloaded_bridge.call("blueprint_action_catalog", {
            **catalog_base, **filters, "limit": 10,
        })
        if not catalog.get("actions"):
            raise AssertionError(f"{family} action missing after restart: {catalog!r}")
        if len(json.dumps(catalog, separators=(",", ":"))) > 32_768:
            raise AssertionError(f"{family} catalog exceeded representative context budget")

    type_by_family = {
        "game_mode_base": "game_mode_base_blueprint",
        "game_mode": "game_mode_blueprint",
        "game_state_base": "game_state_base_blueprint",
        "game_state": "game_state_blueprint",
    }
    for family, expected in phase_fourteen_families.items():
        family_root = _root(reloaded_bridge, expected["asset_path"])
        if family_root.get("snapshot_id") != expected["snapshot_id"] \
                or family_root.get("asset", {}).get("type") != type_by_family[family]:
            raise AssertionError(f"{family} semantic identity changed after restart: {family_root!r}")
        family_graph = _event_graph(reloaded_bridge, expected["asset_path"])
        catalog = reloaded_bridge.call("blueprint_action_catalog", {
            "asset_path": expected["asset_path"],
            "graph_id": family_graph["graph"]["debug"]["graph_guid"],
            "expected_snapshot": family_root["snapshot_id"],
            "node_family": "function_call",
            "owner_class": expected["callable_owner"],
            "function": expected["callable_name"],
            "limit": 5,
        })
        if not catalog.get("actions"):
            raise AssertionError(f"{family} action changed after restart: {catalog!r}")

    game_instance = _root(reloaded_bridge, phase_fifteen_game_instance["asset_path"])
    if game_instance.get("snapshot_id") != phase_fifteen_game_instance["snapshot_id"] \
            or game_instance.get("asset", {}).get("type") != "game_instance_blueprint":
        raise AssertionError(f"GameInstance semantic identity changed after restart: {game_instance!r}")
    shutdown_catalog = reloaded_bridge.call("blueprint_action_catalog", {
        "asset_path": phase_fifteen_game_instance["asset_path"],
        "graph_id": phase_fifteen_game_instance["event_graph_id"],
        "expected_snapshot": game_instance["snapshot_id"],
        "node_family": "event",
        "owner_class": "/Script/Engine.GameInstance",
        "function": "ReceiveShutdown",
        "limit": 5,
    })
    if not shutdown_catalog.get("actions"):
        raise AssertionError(f"GameInstance Shutdown callback changed after restart: {shutdown_catalog!r}")
