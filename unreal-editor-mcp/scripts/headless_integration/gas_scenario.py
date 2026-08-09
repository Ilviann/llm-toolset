"""GAS companion inspection-only scenarios."""

from __future__ import annotations

import uuid

from unreal_editor_mcp.bridge import UnrealBridge
from unreal_editor_mcp.errors import BridgeError, ErrorCode


def verify_gas_inspection(bridge: UnrealBridge) -> None:
    effect_path = "/Game/UnrealMCPGAS/GE_InspectionFixture.GE_InspectionFixture"
    effect = bridge.call("blueprint_inspect", {
        "mode": "inspect",
        "asset_path": effect_path,
        "sections": ["summary", "gameplay_effect"],
        "page_size": 100,
    })
    effect_snapshot = effect.get("snapshot_id")
    sections = {record.get("section") for record in effect.get("records", [])}
    expected_sections = {
        "summary", "gameplay_effect_duration", "gameplay_effect_modifiers",
        "gameplay_effect_executions", "gameplay_effect_stacking", "gameplay_effect_cues",
        "gameplay_effect_tags", "gameplay_effect_granted_abilities",
        "gameplay_effect_additional_effects", "gameplay_effect_requirements",
        "gameplay_effect_components", "gameplay_effect_relationships",
    }
    if not isinstance(effect_snapshot, str) or len(effect_snapshot) != 40 \
            or not expected_sections.issubset(sections):
        raise AssertionError(f"Gameplay Effect inspection is incomplete: {effect!r}")
    repeated = bridge.call("blueprint_inspect", {
        "mode": "inspect", "asset_path": effect_path,
        "sections": ["summary", "gameplay_effect"], "page_size": 100,
    })
    typed_effect_records = [
        record for record in effect.get("records", [])
        if record.get("section") != "summary"
    ]
    repeated_typed_records = [
        record for record in repeated.get("records", [])
        if record.get("section") != "summary"
    ]
    if repeated.get("snapshot_id") != effect_snapshot \
            or repeated_typed_records != typed_effect_records:
        raise AssertionError("Gameplay Effect inspection is not deterministic")

    ability = bridge.call("blueprint_inspect", {
        "mode": "inspect",
        "asset_path": "/Game/UnrealMCPGAS/GA_EffectReferenceFixture.GA_EffectReferenceFixture",
        "sections": ["summary", "gameplay_ability"],
        "page_size": 100,
    })
    effect_refs = next(
        (record for record in ability.get("records", [])
         if record.get("section") == "gameplay_ability_effects"),
        None,
    )
    if not isinstance(effect_refs, dict) \
            or effect_refs.get("cost", {}).get("asset_path") != effect_path \
            or effect_refs.get("cooldown", {}).get("resolved") is not False:
        raise AssertionError(f"Gameplay Ability effect references are incomplete: {ability!r}")

    for tool in ("blueprint_compile", "blueprint_save"):
        try:
            bridge.call(tool, {
                "operation_id": uuid.uuid4().hex,
                "asset_path": effect_path,
                "expected_snapshot": effect_snapshot,
        })
        except BridgeError as error:
            if error.code not in {
                ErrorCode.INVALID_ARGUMENT, ErrorCode.INVALID_PARENT,
                ErrorCode.UNSUPPORTED_ASSET, ErrorCode.WRONG_TYPE,
            }:
                raise
        else:
            raise AssertionError(f"{tool} accepted an inspection-only Gameplay Effect")
    preserved = bridge.call("blueprint_inspect", {
        "mode": "inspect", "asset_path": effect_path,
        "sections": ["summary", "gameplay_effect"], "page_size": 100,
    })
    preserved_typed_records = [
        record for record in preserved.get("records", [])
        if record.get("section") != "summary"
    ]
    if preserved.get("snapshot_id") != effect_snapshot \
            or preserved_typed_records != typed_effect_records:
        raise AssertionError("rejected Gameplay Effect mutation changed the asset")

