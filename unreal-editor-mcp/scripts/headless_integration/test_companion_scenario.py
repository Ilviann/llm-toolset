"""Test-companion exclusion checks for the core asset inspector."""

from unreal_editor_mcp.bridge import UnrealBridge

from scripts.asset_family_conformance import (
    CrossProcessFamilyFixture,
    verify_cross_process_family,
)


def verify_test_companion(bridge: UnrealBridge) -> None:
    for path in (
        "/Game/UnrealMCPCompanion/DA_TestAsset.DA_TestAsset",
        "/Game/UnrealMCPCompanion/BP_TestActor.BP_TestActor",
    ):
        verify_cross_process_family(bridge, CrossProcessFamilyFixture(
            family_id="test-companion-neutral-inspection",
            command="asset_inspect",
            arguments={"asset_path": path},
            expected_fields=((('asset', 'path'), path),),
        ))
