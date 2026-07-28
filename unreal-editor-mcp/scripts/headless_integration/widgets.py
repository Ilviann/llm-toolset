"""Widget Blueprint authoring and restart-verification scenario."""

from __future__ import annotations

import uuid

from unreal_editor_mcp.bridge import UnrealBridge


ASSET_PATH = "/Game/UnrealMCPWidgetTree/WBP_WidgetTree.WBP_WidgetTree"


def _edit(
    bridge: UnrealBridge,
    snapshot: str,
    operation: str,
    **arguments: object,
) -> dict[str, object]:
    return bridge.call("widget_tree_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": ASSET_PATH,
        "expected_snapshot": snapshot,
        "operation": operation,
        **arguments,
    })


def author_widget_scenario(bridge: UnrealBridge) -> dict[str, str]:
    """Create, structure, compile, and save one representative Widget Blueprint."""
    created = bridge.call("blueprint_create", {
        "operation_id": uuid.uuid4().hex,
        "parent_class": "/Script/UMG.UserWidget",
        "package_path": "/Game/UnrealMCPWidgetTree/WBP_WidgetTree",
    })
    if created.get("blueprint_family") != "widget" \
            or created.get("family_capabilities", {}).get("widget_tree") is not True:
        raise AssertionError(f"Widget Blueprint creation contract mismatch: {created!r}")

    root = _edit(
        bridge,
        created["snapshot_id"],
        "set_root",
        widget_class="/Script/UMG.CanvasPanel",
        name="RootCanvas",
    )
    root_id = root.get("widget_id")
    if not isinstance(root_id, str) or len(root_id) != 32:
        raise AssertionError(f"root mutation omitted its stable identity: {root!r}")

    added = _edit(
        bridge,
        root["snapshot_id"],
        "add",
        widget_class="/Script/UMG.TextBlock",
        name="Title",
        target={"kind": "panel", "parent_id": root_id, "index": 0},
    )
    widget_id = added.get("widget_id")
    if not isinstance(widget_id, str) or len(widget_id) != 32:
        raise AssertionError(f"widget mutation omitted its stable identity: {added!r}")

    renamed = _edit(
        bridge,
        added["snapshot_id"],
        "rename",
        widget_id=widget_id,
        new_name="TitleLabel",
    )
    exposed = _edit(
        bridge,
        renamed["snapshot_id"],
        "set_variable",
        widget_id=widget_id,
        is_variable=True,
    )
    defaulted = _edit(
        bridge,
        exposed["snapshot_id"],
        "set_property",
        widget_id=widget_id,
        property_name="RenderOpacity",
        value=0.5,
    )
    compiled = bridge.call("blueprint_compile", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": ASSET_PATH,
        "expected_snapshot": defaulted["snapshot_id"],
    })
    if compiled.get("compile_succeeded") is not True:
        raise AssertionError(f"Widget Blueprint compilation failed: {compiled!r}")
    saved = bridge.call("blueprint_save", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": ASSET_PATH,
        "expected_snapshot": compiled["snapshot_id"],
    })
    if saved.get("saved") is not True or saved.get("package_dirty") is not False:
        raise AssertionError(f"Widget Blueprint save failed: {saved!r}")
    return {
        "asset_path": ASSET_PATH,
        "root_id": root_id,
        "widget_id": widget_id,
        "snapshot_id": saved["snapshot_id"],
    }


def verify_restarted_widgets(
    bridge: UnrealBridge,
    scenario: dict[str, str],
) -> None:
    """Verify stable identities, hierarchy, and defaults after an editor restart."""
    inspection = bridge.call("blueprint_inspect", {
        "mode": "inspect",
        "asset_path": scenario["asset_path"],
        "sections": ["summary", "widget_tree", "widget_defaults"],
        "widget_id": scenario["widget_id"],
        "property_names": ["RenderOpacity"],
        "page_size": 100,
    })
    records = inspection.get("records", [])
    widget = next(
        (record for record in records
         if record.get("section") == "widget"
         and record.get("id") == scenario["widget_id"]),
        None,
    )
    default = next(
        (record for record in records
         if record.get("section") == "widget_default"
         and record.get("widget_id") == scenario["widget_id"]
         and record.get("name") == "RenderOpacity"),
        None,
    )
    if widget is None or widget.get("name") != "TitleLabel" \
            or widget.get("parent_id") != scenario["root_id"] \
            or widget.get("is_variable") is not True:
        raise AssertionError(f"Widget hierarchy did not survive restart: {records!r}")
    if default is None or default.get("supported") is not True \
            or default.get("value") != 0.5:
        raise AssertionError(f"Widget default did not survive restart: {default!r}")
    if inspection.get("snapshot_id") != scenario["snapshot_id"]:
        raise AssertionError("saved Widget Blueprint snapshot changed across restart")
