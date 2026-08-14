"""Unified CommonUI asset-family adapter checks."""

from unreal_editor_mcp.bridge import UnrealBridge


def verify_commonui_inspection(bridge: UnrealBridge) -> None:
    path = "/Game/UnrealMCPCommonUI/WBP_InspectionFixture.WBP_InspectionFixture"
    first = bridge.call("asset_inspect", {"asset_path": path})
    repeated = bridge.call("asset_inspect", {"asset_path": path})
    expected_commonui_sections = {
        "commonui_widget", "commonui_activation", "commonui_references",
    }
    expected_base_sections = {
        "widget_blueprint", "widget", "user_widget", "widget_tree",
        "named_slots", "bindings",
    }
    expected_selectors = expected_commonui_sections | {
        "widget_tree", "widgets", "named_slots", "bindings", "properties",
    }
    if first != repeated or first.get("asset", {}).get("type") != "widget_blueprint" \
            or first.get("asset", {}).get("path") != path \
            or not expected_commonui_sections.issubset(first) \
            or not expected_base_sections.issubset(first) \
            or not expected_selectors.issubset(first.get("selectors", [])) \
            or "limitations" in first:
        raise AssertionError(f"CommonUI adapter root inspection changed: {first!r}")
    for section in sorted(expected_commonui_sections):
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
