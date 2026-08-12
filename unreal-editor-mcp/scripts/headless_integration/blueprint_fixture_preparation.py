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
    inspection = bridge.call("asset_inspect", {
        "asset_path": "/Game/UnrealMCPPhase2/BP_InspectionFixture.BP_InspectionFixture",
    })
    loaded_snapshot = inspection.get("snapshot_id")
    if not isinstance(loaded_snapshot, str) or len(loaded_snapshot) != 40:
        raise AssertionError("reloaded Phase 2 fixture did not report a structural snapshot")
    if inspection.get("asset", {}).get("type") != "actor_blueprint" \
            or not inspection.get("event_graphs") or not inspection.get("variables") \
            or not inspection.get("components"):
        raise AssertionError(f"live semantic inspection omitted required structure: {inspection!r}")

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
        "phase_two_loaded_inspection": inspection,
        "created": created,
        "phase_fourteen_families": author_phase_fourteen_families(bridge),
        "phase_fifteen_game_instance": author_phase_fifteen_game_instance(bridge),
    }
