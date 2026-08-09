"""Writable disposable test-companion scenario."""

from __future__ import annotations

import uuid

from unreal_editor_mcp.bridge import UnrealBridge
from unreal_editor_mcp.errors import BridgeError, ErrorCode


def verify_test_companion(bridge: UnrealBridge) -> None:
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

