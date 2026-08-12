"""Test-companion exclusion checks for the core asset inspector."""

from unreal_editor_mcp.bridge import UnrealBridge


def verify_test_companion(bridge: UnrealBridge) -> None:
    for path in (
        "/Game/UnrealMCPCompanion/DA_TestAsset.DA_TestAsset",
        "/Game/UnrealMCPCompanion/BP_TestActor.BP_TestActor",
    ):
        inspected = bridge.call("asset_inspect", {"asset_path": path})
        if not isinstance(inspected.get("snapshot_id"), str):
            raise AssertionError(f"core asset identity is unavailable for companion asset: {inspected!r}")
