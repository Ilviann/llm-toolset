"""Animation Blueprint production-socket inspection and restart checks."""

from unreal_editor_mcp.bridge import UnrealBridge


ASSET_PATH = "/Game/UnrealMCPAnimation/ABP_InspectionFixture.ABP_InspectionFixture"


def inspect_animation_fixture(bridge: UnrealBridge) -> str:
    first = bridge.call("asset_inspect", {"asset_path": ASSET_PATH})
    repeated = bridge.call("asset_inspect", {"asset_path": ASSET_PATH})
    graphs = first.get("animation_graphs")
    machines = first.get("state_machines")
    if first != repeated or first.get("asset", {}).get("type") != "animation_blueprint" \
            or first.get("animation_blueprint", {}).get("mode") != "regular" \
            or not isinstance(graphs, list) or not graphs \
            or not isinstance(machines, list) or not machines \
            or "limitations" in first:
        raise AssertionError(f"Animation Blueprint root inspection changed: {first!r}")
    graph_selector = graphs[0].get("selector")
    machine_selector = machines[0].get("selector")
    if not isinstance(graph_selector, str) or not isinstance(machine_selector, str):
        raise AssertionError(f"Animation selectors are unavailable: {first!r}")
    graph = bridge.call("asset_inspect", {
        "asset_path": ASSET_PATH, "selector": graph_selector,
    })
    if graph.get("snapshot_id") != first.get("snapshot_id") \
            or graph.get("selection", {}).get("selector") != graph_selector \
            or graph.get("graph", {}).get("graph_status", {}).get("complete") is not True \
            or graph.get("animation_graph", {}).get("name") != graphs[0].get("name"):
        raise AssertionError(f"Animation pose graph inspection changed: {graph!r}")
    machine = bridge.call("asset_inspect", {
        "asset_path": ASSET_PATH, "selector": machine_selector,
    })
    topology = machine.get("state_machine", {})
    if machine.get("snapshot_id") != first.get("snapshot_id") \
            or machine.get("selection", {}).get("selector") != machine_selector \
            or len(topology.get("states", [])) != 1:
        raise AssertionError(f"Animation state-machine inspection changed: {machine!r}")
    snapshot = first.get("snapshot_id")
    if not isinstance(snapshot, str) or len(snapshot) != 40:
        raise AssertionError(f"Animation snapshot is unavailable: {first!r}")
    return snapshot


def verify_restarted_animation(bridge: UnrealBridge, snapshot: str) -> None:
    if inspect_animation_fixture(bridge) != snapshot:
        raise AssertionError("Animation Blueprint snapshot changed across restart")
