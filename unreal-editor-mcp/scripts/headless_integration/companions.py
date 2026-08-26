"""Production-socket acceptance for the fixture and released companions."""

from __future__ import annotations

import uuid

from unreal_editor_mcp.bridge import UnrealBridge
from unreal_editor_mcp.errors import BridgeError, ErrorCode


def verify_companion_scenario(bridge: UnrealBridge, capabilities: dict[str, object]) -> None:
    companions = capabilities.get("companions")
    if capabilities.get("companion_api_version") != 1 or not isinstance(companions, list):
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

    reflected = bridge.call("blueprint_inspect", {
        "mode": "inspect",
        "asset_path": effect_path,
        "sections": ["class_defaults"],
        "property_names": ["Modifiers"],
        "page_size": 10,
    })
    reflected_records = reflected.get("records", [])
    modifier_default = reflected_records[0] \
        if isinstance(reflected_records, list) and len(reflected_records) == 1 \
        else None
    modifier_values = modifier_default.get("value") \
        if isinstance(modifier_default, dict) else None
    modifier_fields = modifier_values[0].get("fields") \
        if isinstance(modifier_values, list) and modifier_values \
        and isinstance(modifier_values[0], dict) else None
    if not isinstance(modifier_default, dict) \
            or modifier_default.get("section") != "class_default" \
            or modifier_default.get("name") != "Modifiers" \
            or modifier_default.get("supported") is not True \
            or modifier_default.get("type") != "array" \
            or not isinstance(modifier_fields, dict) \
            or not all(name in modifier_fields for name in (
                "Attribute", "ModifierOp", "ModifierMagnitude",
                "EvaluationChannelSettings", "SourceTags", "TargetTags",
            )):
        raise AssertionError(
            f"Gameplay Effect Modifiers reflected default is incomplete: {reflected!r}"
        )

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

    cases = (
        ("blueprint_inspect", "inspect_test_asset", "blueprint_default_edit",
         "set_test_asset_value", "/Game/UnrealMCPCompanion/DA_TestAsset.DA_TestAsset", 101),
        ("blueprint_inspect", "inspect_test_component", "blueprint_component_edit",
         "set_test_component_value", "/Game/UnrealMCPCompanion/BP_TestActor.BP_TestActor", 202),
        ("blueprint_inspect", "inspect_test_contribution", "blueprint_default_edit",
         "set_test_contribution_value", "/Game/UnrealMCPCompanion/BP_TestActor.BP_TestActor", 303),
    )
    for read_tool, read_operation, mutation_tool, mutation_operation, asset_path, value in cases:
        common = {
            "extension_id": "unreal-mcp-test",
            "extension_schema_revision": 1,
            "asset_path": asset_path,
        }
        inspected = bridge.call(read_tool, {**common, "operation": read_operation})
        snapshot = inspected.get("snapshot")
        if not isinstance(snapshot, str) or len(snapshot) != 40:
            raise AssertionError(f"companion inspection snapshot is invalid: {inspected!r}")
        mutation = {
            **common,
            "operation": mutation_operation,
            "operation_id": uuid.uuid4().hex,
            "expected_snapshot": snapshot,
            "value": value,
        }
        changed = bridge.call(mutation_tool, mutation)
        replayed = bridge.call(mutation_tool, mutation)
        if changed.get("value") != value or replayed != changed \
                or changed.get("operation_state") != "committed":
            raise AssertionError(f"companion mutation/replay failed: {changed!r} {replayed!r}")
        try:
            bridge.call(mutation_tool, {
                **mutation,
                "operation_id": uuid.uuid4().hex,
                "value": value + 1,
            })
        except BridgeError as error:
            if error.code != ErrorCode.STALE_PRECONDITION:
                raise
        else:
            raise AssertionError("stale companion mutation was accepted")
