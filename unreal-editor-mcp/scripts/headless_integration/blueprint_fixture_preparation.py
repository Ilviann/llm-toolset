"""Prepare Blueprint fixtures for the cross-process integration scenario."""

from __future__ import annotations

import uuid

from unreal_editor_mcp.bridge import UnrealBridge

from .blueprint_declarations import (
    author_phase_fifteen_game_instance,
    author_phase_fourteen_families,
)


def prepare_blueprint_scenario(bridge: UnrealBridge) -> dict[str, object]:
    """Inspect the prepared fixture and create the authored Blueprint families."""
    discovery = bridge.call("blueprint_inspect", {
        "mode": "discover",
        "package_path": "/Game/UnrealMCPPhase2",
        "asset_name": "BP_InspectionFixture",
    })
    if not any(record.get("section") == "asset" for record in discovery.get("records", [])):
        raise AssertionError("saved Actor Blueprint was not discoverable after editor restart")
    inspection = bridge.call("blueprint_inspect", {
        "mode": "inspect",
        "asset_path": "/Game/UnrealMCPPhase2/BP_InspectionFixture.BP_InspectionFixture",
        "sections": [
            "summary", "parent_class", "compile_state", "components", "variables",
            "graphs", "nodes", "pins", "connections",
        ],
        "page_size": 100,
    })
    loaded_snapshot = inspection.get("snapshot_id")
    if not isinstance(loaded_snapshot, str) or len(loaded_snapshot) != 40:
        raise AssertionError("reloaded Phase 2 fixture did not report a structural snapshot")
    found = {record.get("section") for record in inspection.get("records", [])}
    if not {"summary", "component", "variable", "graph", "node", "pin"}.issubset(found):
        raise AssertionError(f"live inspection omitted required structure: {sorted(found)!r}")

    created = bridge.call("blueprint_create", {
        "operation_id": uuid.uuid4().hex,
        "parent_class": "/Script/Engine.Actor",
        "package_path": "/Game/UnrealMCPPhase4/BP_ComponentFixture",
    })
    if created.get("compile_succeeded") is not True or created.get("saved") is not True:
        raise AssertionError(f"Actor Blueprint creation did not compile and save: {created!r}")
    if created.get("parent_class") != "/Script/Engine.Actor" \
            or created.get("package_dirty") is not False:
        raise AssertionError(f"Actor Blueprint creation contract mismatch: {created!r}")
    return {
        "phase_two_loaded_snapshot": loaded_snapshot,
        "created": created,
        "phase_fourteen_families": author_phase_fourteen_families(bridge),
        "phase_fifteen_game_instance": author_phase_fifteen_game_instance(bridge),
    }
