"""Companion admission and capability-matrix checks."""

from __future__ import annotations

import uuid

from unreal_editor_mcp.bridge import UnrealBridge
from unreal_editor_mcp.asset_family_catalog import COMPANION_API_VERSION
from unreal_editor_mcp.errors import BridgeError, ErrorCode


def verify_companion_admission(capabilities: dict[str, object]) -> None:
    companions = capabilities.get("companions")
    if capabilities.get("companion_api_version") != COMPANION_API_VERSION \
            or not isinstance(companions, list):
        raise AssertionError(f"companion capabilities are missing: {capabilities!r}")
    fixture = next(
        (item for item in companions if isinstance(item, dict)
         and item.get("extension_id") == "unreal-mcp-test"),
        None,
    )
    if fixture is None or fixture.get("ready") is not True \
            or len(fixture.get("contributions", [])) != 6:
        raise AssertionError(f"test companion is not exactly registered: {fixture!r}")
    gas = next(
        (item for item in companions if isinstance(item, dict)
         and item.get("extension_id") == "unreal-mcp-gas"),
        None,
    )
    if gas is None or gas.get("ready") is not True \
            or gas.get("read_support") is not True \
            or gas.get("mutation_support") is not False \
            or gas.get("contributions") != []:
        raise AssertionError(f"GAS inspection companion is not exactly registered: {gas!r}")
    gas_families = {
        item.get("family_id"): item for item in gas.get("asset_families", [])
        if isinstance(item, dict)
    }
    if set(gas_families) != {
            "attribute_set", "gameplay_ability", "gameplay_cue_notify_actor",
            "gameplay_cue_notify_static", "gameplay_effect",
            "gameplay_effect_execution_calculation",
            "gameplay_mod_magnitude_calculation",
            } \
            or any(item.get("operations") != {
                "inspect": True, "create": False, "edit": False,
            } for item in gas_families.values()):
        raise AssertionError(f"GAS asset families are not exactly read-only: {gas_families!r}")
    commonui = next(
        (item for item in companions if isinstance(item, dict)
         and item.get("extension_id") == "unreal-mcp-commonui"),
        None,
    )
    if commonui is None or commonui.get("ready") is not True \
            or commonui.get("read_support") is not True \
            or commonui.get("mutation_support") is not False \
            or commonui.get("contributions") != []:
        raise AssertionError(f"CommonUI inspection companion is not exactly registered: {commonui!r}")
    commonui_families = commonui.get("asset_families", [])
    if len(commonui_families) != 1 \
            or commonui_families[0].get("family_id") != "commonui_widget" \
            or commonui_families[0].get("operations") != {
                "inspect": True, "create": False, "edit": False,
            }:
        raise AssertionError(
            f"CommonUI asset family is not exactly read-only: {commonui_families!r}"
        )
    enhanced_input = next(
        (item for item in companions if isinstance(item, dict)
         and item.get("extension_id") == "unreal-mcp-enhanced-input"),
        None,
    )
    if enhanced_input is None or enhanced_input.get("ready") is not True \
            or enhanced_input.get("read_support") is not True \
            or enhanced_input.get("mutation_support") is not False \
            or enhanced_input.get("contributions") != []:
        raise AssertionError(
            f"Enhanced Input inspection companion is not exactly registered: {enhanced_input!r}"
        )
    enhanced_input_families = {
        item.get("family_id"): item
        for item in enhanced_input.get("asset_families", []) if isinstance(item, dict)
    }
    if set(enhanced_input_families) != {
            "input_action", "input_mapping_context", "input_modifier_blueprint",
            "input_trigger_blueprint", "player_mappable_input_config",
            } or any(item.get("operations") != {
                "inspect": True, "create": False, "edit": False,
            } for item in enhanced_input_families.values()):
        raise AssertionError(
            f"Enhanced Input asset families are not exactly read-only: {enhanced_input_families!r}"
        )
    ai = next(
        (item for item in companions if isinstance(item, dict)
         and item.get("extension_id") == "unreal-mcp-ai"),
        None,
    )
    if ai is None or ai.get("ready") is not True \
            or ai.get("read_support") is not True \
            or ai.get("mutation_support") is not False \
            or ai.get("contributions") != []:
        raise AssertionError(f"AI inspection companion is not exactly registered: {ai!r}")
    ai_families = {
        item.get("family_id"): item
        for item in ai.get("asset_families", []) if isinstance(item, dict)
    }
    if set(ai_families) != {
            "behavior_tree", "blackboard", "bt_decorator_blueprint",
            "bt_service_blueprint", "bt_task_blueprint", "environment_query",
            "eqs_context_blueprint", "eqs_generator_blueprint",
            } or any(item.get("operations") != {
                "inspect": True, "create": False, "edit": False,
            } for item in ai_families.values()):
        raise AssertionError(f"AI asset families are not exactly read-only: {ai_families!r}")
