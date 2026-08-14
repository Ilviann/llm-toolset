"""Widget authoring lifecycle without the removed reconstruction inspector."""

from __future__ import annotations

import uuid

from unreal_editor_mcp.bridge import UnrealBridge

from scripts.asset_family_conformance import (
    CrossProcessFamilyFixture,
    verify_restart_read_back,
)


ASSET_PATH = "/Game/UnrealMCPWidgetTree/WBP_WidgetTree.WBP_WidgetTree"


def _edit(bridge: UnrealBridge, snapshot: str, operation: str, **arguments: object) -> dict[str, object]:
    return bridge.call("widget_tree_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": ASSET_PATH,
        "expected_snapshot": snapshot,
        "operation": operation,
        **arguments,
    })


def author_widget_scenario(bridge: UnrealBridge) -> dict[str, str]:
    created = bridge.call("blueprint_create", {
        "operation_id": uuid.uuid4().hex,
        "parent_class": "/Script/UMG.UserWidget",
        "package_path": "/Game/UnrealMCPWidgetTree/WBP_WidgetTree",
    })
    root = _edit(
        bridge, created["snapshot_id"], "set_root",
        widget_class="/Script/UMG.CanvasPanel", name="RootCanvas",
    )
    added = _edit(
        bridge, root["snapshot_id"], "add",
        widget_class="/Script/UMG.TextBlock", name="Title",
        target={"kind": "panel", "parent_id": root["widget_id"], "index": 0},
    )
    renamed = _edit(
        bridge, added["snapshot_id"], "rename",
        widget_id=added["widget_id"], new_name="TitleLabel",
    )
    exposed = _edit(
        bridge, renamed["snapshot_id"], "set_variable",
        widget_id=added["widget_id"], is_variable=True,
    )
    compiled = bridge.call("blueprint_compile", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": ASSET_PATH,
        "expected_snapshot": exposed["snapshot_id"],
    })
    saved = bridge.call("blueprint_save", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": ASSET_PATH,
        "expected_snapshot": compiled["snapshot_id"],
    })
    if saved.get("saved") is not True:
        raise AssertionError(f"Widget Blueprint save failed: {saved!r}")
    inspected = bridge.call("asset_inspect", {"asset_path": ASSET_PATH})
    return {"asset_path": ASSET_PATH, "snapshot_id": inspected["snapshot_id"]}


def verify_restarted_widgets(bridge: UnrealBridge, scenario: dict[str, str]) -> None:
    verify_restart_read_back(
        bridge,
        CrossProcessFamilyFixture(
            family_id="widget-blueprint",
            command="asset_inspect",
            arguments={"asset_path": scenario["asset_path"]},
            expected_fields=(
                (("asset", "path"), scenario["asset_path"]),
                (("asset", "type"), "widget_blueprint"),
                (("widget_tree", "widget_count"), 2),
            ),
        ),
        scenario["snapshot_id"],
    )
