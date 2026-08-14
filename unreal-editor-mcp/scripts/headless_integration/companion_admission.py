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
            or len(gas.get("contributions", [])) != 2:
        raise AssertionError(f"GAS inspection companion is not exactly registered: {gas!r}")
    features = capabilities.get("features", {})
    if features.get("gas_ability_blueprints_inspection") is not True \
            or features.get("gas_ability_blueprints_mutation") is not False \
            or features.get("gas_gameplay_effects_inspection") is not True \
            or features.get("gas_gameplay_effects_mutation") is not False:
        raise AssertionError(f"GAS read/mutation capabilities are incorrect: {features!r}")
    commonui = next(
        (item for item in companions if isinstance(item, dict)
         and item.get("extension_id") == "unreal-mcp-commonui"),
        None,
    )
    if commonui is None or commonui.get("ready") is not True \
            or commonui.get("read_support") is not True \
            or commonui.get("mutation_support") is not False \
            or len(commonui.get("contributions", [])) != 1:
        raise AssertionError(f"CommonUI inspection companion is not exactly registered: {commonui!r}")
    if features.get("commonui_widget_blueprints_inspection") is not True \
            or features.get("commonui_widget_blueprints_mutation") is not False:
        raise AssertionError(f"CommonUI read/mutation capabilities are incorrect: {features!r}")
    commonui_family = next(
        (item for item in capabilities.get("blueprint_families", [])
         if isinstance(item, dict) and item.get("family") == "commonui_widget"),
        None,
    )
    commonui_operations = (
        commonui_family.get("operations", {}) if isinstance(commonui_family, dict) else {}
    )
    if commonui_operations.get("discover") is not True \
            or commonui_operations.get("inspect") is not True \
            or any(commonui_operations.get(name) is not False for name in (
                "create", "compile", "save", "class_defaults", "components",
                "widget_tree", "member_variables", "functions", "macros",
                "custom_events", "action_catalog", "graph_edit",
            )):
        raise AssertionError(f"CommonUI family is not inspection-only: {commonui_family!r}")
    gas_family = next(
        (item for item in capabilities.get("blueprint_families", [])
         if isinstance(item, dict) and item.get("family") == "gameplay_ability"),
        None,
    )
    operations = gas_family.get("operations", {}) if isinstance(gas_family, dict) else {}
    if operations.get("discover") is not True or operations.get("inspect") is not True \
            or any(operations.get(name) is not False for name in (
                "create", "compile", "save", "class_defaults", "member_variables",
                "action_catalog", "graph_edit",
            )):
        raise AssertionError(f"GAS family is not inspection-only: {gas_family!r}")
    effect_family = next(
        (item for item in capabilities.get("blueprint_families", [])
         if isinstance(item, dict) and item.get("family") == "gameplay_effect"),
        None,
    )
    effect_operations = effect_family.get("operations", {}) if isinstance(effect_family, dict) else {}
    if effect_operations.get("discover") is not True or effect_operations.get("inspect") is not True \
            or any(effect_operations.get(name) is not False for name in (
                "create", "compile", "save", "class_defaults", "components",
                "member_variables", "functions", "macros", "custom_events",
                "action_catalog", "graph_edit",
            )):
        raise AssertionError(f"Gameplay Effect family is not inspection-only: {effect_family!r}")
