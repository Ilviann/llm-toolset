"""GAS exclusion checks for the core asset inspector."""

from unreal_editor_mcp.bridge import UnrealBridge


def verify_gas_inspection(bridge: UnrealBridge) -> None:
    paths = (
        "/Game/UnrealMCPGAS/GE_InspectionFixture.GE_InspectionFixture",
        "/Game/UnrealMCPGAS/GA_EffectReferenceFixture.GA_EffectReferenceFixture",
    )
    for path in paths:
        first = bridge.call("asset_inspect", {"asset_path": path})
        repeated = bridge.call("asset_inspect", {"asset_path": path})
        if first != repeated or "unsupported_family" not in first.get("limitations", []):
            raise AssertionError(f"GAS core exclusion changed for {path}: {first!r}")
