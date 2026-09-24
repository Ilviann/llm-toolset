"""Production-bridge coverage for reflected and library inspection selectors."""

from unreal_editor_mcp.bridge import UnrealBridge


def verify_expanded_inspection(bridge: UnrealBridge) -> dict[str, str]:
    snapshots = {}
    for name, family in (
        ("BFL_InspectionFixture", "function_library_blueprint"),
        ("BML_InspectionFixture", "macro_library_blueprint"),
    ):
        path = f"/Game/UnrealMCPPhase2/{name}.{name}"
        root = bridge.call("asset_inspect", {"asset_path": path})
        if root["asset"]["type"] != family:
            raise AssertionError(f"wrong library family: {root!r}")
        page = bridge.call("asset_inspect", {
            "asset_path": path, "selector": "graphs", "page_size": 1, "page_index": 0,
        })
        item = page["collection"]["items"][0]
        detail = bridge.call("asset_inspect", {"asset_path": path, "selector": item["selector"]})
        if not detail.get("graph") or detail["snapshot_id"] != root["snapshot_id"] \
                or page["snapshot_id"] != root["snapshot_id"]:
            raise AssertionError("library summary/page/detail snapshots disagree")
        snapshots[path] = root["snapshot_id"]
    path = "/Game/UnrealMCPPhase2/BP_InspectionFixture.BP_InspectionFixture"
    root = bridge.call("asset_inspect", {"asset_path": path})
    selected = bridge.call("asset_inspect", {"asset_path": path, "selector": "class_defaults/NetPriority"})
    if selected["snapshot_id"] != root["snapshot_id"] or "value" not in selected:
        raise AssertionError("targeted class property did not preserve root snapshot")
    components = bridge.call("asset_inspect", {"asset_path": path, "selector": "components"})
    component = components["collection"]["items"][0]
    detail = bridge.call("asset_inspect", {"asset_path": path, "selector": f"components/{component['id']}"})
    if not detail["component"].get("template_origin") or detail["snapshot_id"] != root["snapshot_id"]:
        raise AssertionError("component identity/provenance inspection failed")
    return snapshots
