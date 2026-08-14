"""Unified CommonUI asset-family adapter checks."""

from unreal_editor_mcp.bridge import UnrealBridge


def verify_commonui_inspection(bridge: UnrealBridge) -> None:
    path = "/Game/UnrealMCPCommonUI/WBP_InspectionFixture.WBP_InspectionFixture"
    first = bridge.call("asset_inspect", {"asset_path": path})
    repeated = bridge.call("asset_inspect", {"asset_path": path})
    expected_sections = {
        "commonui_widget", "commonui_activation", "commonui_references",
    }
    if first != repeated or first.get("asset", {}).get("type") != "unsupported_blueprint" \
            or first.get("asset", {}).get("path") != path \
            or not expected_sections.issubset(first) \
            or set(first.get("selectors", [])) != expected_sections \
            or "limitations" in first:
        raise AssertionError(f"CommonUI adapter root inspection changed: {first!r}")
    for section in sorted(expected_sections):
        selected = bridge.call("asset_inspect", {
            "asset_path": path, "selector": section,
        })
        if selected.get("snapshot_id") != first.get("snapshot_id") \
                or selected.get("selection", {}).get("selector") != section \
                or selected.get("asset", {}).get("path") != path \
                or section not in selected:
            raise AssertionError(
                f"CommonUI adapter selector inspection changed for {section}: {selected!r}"
            )
