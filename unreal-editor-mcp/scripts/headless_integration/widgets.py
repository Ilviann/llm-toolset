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
    """Create, lay out, style, bind, compile, and save one Widget Blueprint."""
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
    tree = bridge.call("blueprint_inspect", {
        "mode": "inspect",
        "asset_path": ASSET_PATH,
        "sections": ["widget_tree"],
        "widget_id": widget_id,
        "page_size": 100,
    })
    widget_record = next(
        (record for record in tree.get("records", [])
         if record.get("section") == "widget"
         and record.get("id") == widget_id),
        None,
    )
    slot_id = widget_record.get("slot_id") if widget_record else None
    if not isinstance(slot_id, str) or len(slot_id) != 32:
        raise AssertionError(f"widget inspection omitted its slot identity: {tree!r}")
    laid_out = _edit(
        bridge,
        exposed["snapshot_id"],
        "set_slot",
        slot_id=slot_id,
        property_name="ZOrder",
        value=5,
    )
    styled = _edit(
        bridge,
        laid_out["snapshot_id"],
        "set_style",
        widget_id=widget_id,
        property_name="Text",
        value="Status",
    )
    button = _edit(
        bridge,
        styled["snapshot_id"],
        "add",
        widget_class="/Script/UMG.Button",
        name="ActionButton",
        target={"kind": "panel", "parent_id": root_id, "index": 1},
    )
    button_id = button.get("widget_id")
    if not isinstance(button_id, str) or len(button_id) != 32:
        raise AssertionError(f"button mutation omitted its stable identity: {button!r}")
    exposed_button = _edit(
        bridge,
        button["snapshot_id"],
        "set_variable",
        widget_id=button_id,
        is_variable=True,
    )
    compiled = bridge.call("blueprint_compile", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": ASSET_PATH,
        "expected_snapshot": exposed_button["snapshot_id"],
    })
    if compiled.get("compile_succeeded") is not True:
        raise AssertionError(f"Widget Blueprint compilation failed: {compiled!r}")
    bound = _edit(
        bridge,
        compiled["snapshot_id"],
        "bind_event",
        widget_id=button_id,
        delegate_name="OnClicked",
    )
    binding_inspection = bridge.call("blueprint_inspect", {
        "mode": "inspect",
        "asset_path": ASSET_PATH,
        "sections": ["widget_bindings"],
        "page_size": 100,
    })
    event_binding = next(
        (record for record in binding_inspection.get("records", [])
         if record.get("section") == "widget_bindings"
         and record.get("record_type") == "event_binding"
         and record.get("widget_id") == button_id
         and record.get("delegate_name") == "OnClicked"),
        None,
    )
    if event_binding is None or event_binding.get("cost") != "event_driven":
        raise AssertionError(
            f"Designer event binding did not read back: {binding_inspection!r}"
        )
    saved = bridge.call("blueprint_save", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": ASSET_PATH,
        "expected_snapshot": bound["snapshot_id"],
    })
    if saved.get("saved") is not True or saved.get("package_dirty") is not False:
        raise AssertionError(f"Widget Blueprint save failed: {saved!r}")
    return {
        "asset_path": ASSET_PATH,
        "root_id": root_id,
        "widget_id": widget_id,
        "slot_id": slot_id,
        "button_id": button_id,
        "snapshot_id": saved["snapshot_id"],
    }


def verify_restarted_widgets(
    bridge: UnrealBridge,
    scenario: dict[str, str],
) -> None:
    """Verify stable hierarchy, layout, presentation, and event binding after restart."""
    inspection = bridge.call("blueprint_inspect", {
        "mode": "inspect",
        "asset_path": scenario["asset_path"],
        "sections": ["summary", "widget_tree", "widget_defaults"],
        "widget_id": scenario["widget_id"],
        "property_names": ["Text"],
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
         and record.get("name") == "Text"),
        None,
    )
    slot = next(
        (record for record in records
         if record.get("section") == "widget_slot"
         and record.get("id") == scenario["slot_id"]),
        None,
    )
    bindings = bridge.call("blueprint_inspect", {
        "mode": "inspect",
        "asset_path": scenario["asset_path"],
        "sections": ["widget_bindings"],
        "page_size": 100,
    })
    event_binding = next(
        (record for record in bindings.get("records", [])
         if record.get("section") == "widget_bindings"
         and record.get("record_type") == "event_binding"
         and record.get("widget_id") == scenario["button_id"]
         and record.get("delegate_name") == "OnClicked"),
        None,
    )
    if widget is None or widget.get("name") != "TitleLabel" \
            or widget.get("parent_id") != scenario["root_id"] \
            or widget.get("is_variable") is not True:
        raise AssertionError(f"Widget hierarchy did not survive restart: {records!r}")
    if default is None or default.get("supported") is not True \
            or default.get("value") != "Status":
        raise AssertionError(f"Widget presentation did not survive restart: {default!r}")
    if slot is None or slot.get("layout", {}).get("ZOrder") != 5:
        raise AssertionError(f"Widget layout did not survive restart: {slot!r}")
    if event_binding is None or event_binding.get("cost") != "event_driven":
        raise AssertionError(f"Designer event did not survive restart: {event_binding!r}")
    if inspection.get("snapshot_id") != scenario["snapshot_id"]:
        raise AssertionError("saved Widget Blueprint snapshot changed across restart")
    if bindings.get("snapshot_id") != scenario["snapshot_id"]:
        raise AssertionError("saved Widget binding snapshot changed across restart")
