"""CommonUI exclusion check for the core asset inspector."""

from unreal_editor_mcp.bridge import UnrealBridge


def verify_commonui_inspection(bridge: UnrealBridge) -> None:
    path = "/Game/UnrealMCPCommonUI/WBP_InspectionFixture.WBP_InspectionFixture"
    first = bridge.call("asset_inspect", {"asset_path": path})
    repeated = bridge.call("asset_inspect", {"asset_path": path})
    if first != repeated or first.get("asset", {}).get("type") != "unsupported_blueprint" \
            or "unsupported_family" not in first.get("limitations", []):
        raise AssertionError(f"CommonUI core exclusion changed: {first!r}")
