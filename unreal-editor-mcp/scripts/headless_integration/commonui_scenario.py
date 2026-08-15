"""Unified CommonUI asset-family adapter checks."""

from unreal_editor_mcp.bridge import UnrealBridge


def verify_commonui_inspection(bridge: UnrealBridge) -> None:
    path = "/Game/UnrealMCPCommonUI/WBP_InspectionFixture.WBP_InspectionFixture"
    first = bridge.call("asset_inspect", {"asset_path": path})
    repeated = bridge.call("asset_inspect", {"asset_path": path})
    expected_root_sections = {
        "commonui_widget", "commonui_activation", "commonui_references",
    }
    expected_commonui_sections = expected_root_sections | {
        "commonui_widgets",
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
    for section in sorted(expected_root_sections):
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
    widgets = bridge.call("asset_inspect", {
        "asset_path": path, "selector": "commonui_widgets",
        "page_size": 1, "page_index": 0,
    })
    widget_records = widgets.get("commonui_widgets")
    if not isinstance(widget_records, list) or len(widget_records) != 1 \
            or widget_records[0].get("name") != "FixtureCommonText" \
            or widget_records[0].get("family") != "text" \
            or widget_records[0].get("properties", {}).get("style", {}).get("type") \
            != "reference" \
            or widgets.get("page", {}).get("total_items") != 1 \
            or widgets.get("snapshot_id") != first.get("snapshot_id"):
        raise AssertionError(f"CommonUI widget page changed: {widgets!r}")
    detail = bridge.call("asset_inspect", {
        "asset_path": path,
        "selector": "commonui_widgets/FixtureCommonText",
    })
    if detail.get("commonui_widget_detail", {}).get("widget_id") \
            != widget_records[0].get("widget_id") \
            or detail.get("snapshot_id") != first.get("snapshot_id"):
        raise AssertionError(f"CommonUI widget detail changed: {detail!r}")
