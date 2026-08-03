"""Production-socket acceptance for the disposable companion fixture."""

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
            or len(gas.get("contributions", [])) != 1:
        raise AssertionError(f"GAS inspection companion is not exactly registered: {gas!r}")
    features = capabilities.get("features", {})
    if features.get("gas_ability_blueprints_inspection") is not True \
            or features.get("gas_ability_blueprints_mutation") is not False:
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
