"""Production-socket Unreal AI asset inspection checks."""

from unreal_editor_mcp.bridge import UnrealBridge


FIXTURES = (
    ("/Game/UnrealMCPAI/BB_InspectionFixture.BB_InspectionFixture", "blackboard"),
    ("/Game/UnrealMCPAI/BT_InspectionFixture.BT_InspectionFixture", "behavior_tree"),
    ("/Game/UnrealMCPAI/EQS_InspectionFixture.EQS_InspectionFixture", "environment_query"),
    ("/Game/UnrealMCPAI/BP_BTTaskFixture.BP_BTTaskFixture", "bt_task_blueprint"),
    ("/Game/UnrealMCPAI/BP_BTDecoratorFixture.BP_BTDecoratorFixture", "bt_decorator_blueprint"),
    ("/Game/UnrealMCPAI/BP_BTServiceFixture.BP_BTServiceFixture", "bt_service_blueprint"),
    ("/Game/UnrealMCPAI/BP_EQSGeneratorFixture.BP_EQSGeneratorFixture", "eqs_generator_blueprint"),
    ("/Game/UnrealMCPAI/BP_EQSContextFixture.BP_EQSContextFixture", "eqs_context_blueprint"),
)


def inspect_ai_fixtures(bridge: UnrealBridge) -> dict[str, str]:
    snapshots: dict[str, str] = {}
    for path, section in FIXTURES:
        first = bridge.call("asset_inspect", {"asset_path": path})
        repeated = bridge.call("asset_inspect", {"asset_path": path})
        if first != repeated or first.get("asset", {}).get("path") != path \
                or section not in first or section not in first.get("selectors", []) \
                or "limitations" in first:
            raise AssertionError(f"AI root inspection changed for {path}: {first!r}")
        selected = bridge.call("asset_inspect", {
            "asset_path": path, "selector": section,
        })
        if selected.get("snapshot_id") != first.get("snapshot_id") \
                or selected.get("selection", {}).get("selector") != section \
                or selected.get("asset", {}).get("path") != path \
                or section not in selected:
            raise AssertionError(f"AI selector inspection changed for {path}: {selected!r}")
        snapshots[path] = first["snapshot_id"]

    blackboard = bridge.call("asset_inspect", {"asset_path": FIXTURES[0][0]})["blackboard"]
    keys = blackboard.get("keys")
    key_names = [key.get("name") for key in keys] if isinstance(keys, list) else []
    if key_names[-2:] != ["CanSeeTarget", "TargetActor"] \
            or blackboard.get("runtime_values_excluded") is not True:
        raise AssertionError(f"Blackboard semantic schema changed: {blackboard!r}")
    tree = bridge.call("asset_inspect", {"asset_path": FIXTURES[1][0]})["behavior_tree"]
    if tree.get("node_count", 0) < 4 or len(tree.get("child_edges", [])) != 1 \
            or tree.get("blackboard", {}).get("object_path") != FIXTURES[0][0] \
            or tree.get("runtime_state_excluded") is not True:
        raise AssertionError(f"Behavior Tree topology changed: {tree!r}")
    query = bridge.call("asset_inspect", {"asset_path": FIXTURES[2][0]})["environment_query"]
    if query.get("option_count") != 1 or query.get("test_count") != 1 \
            or query.get("runtime_execution_excluded") is not True:
        raise AssertionError(f"EQS topology changed: {query!r}")
    for path, section in FIXTURES[3:]:
        block = bridge.call("asset_inspect", {"asset_path": path})[section]
        if block.get("ordinary_blueprint_semantics_composed") is not True \
                or block.get("runtime_execution_excluded") is not True:
            raise AssertionError(f"AI Blueprint composition changed for {path}: {block!r}")
    return snapshots


def verify_restarted_ai(bridge: UnrealBridge, snapshots: dict[str, str]) -> None:
    current = inspect_ai_fixtures(bridge)
    if current != snapshots:
        raise AssertionError(f"AI snapshots changed after restart: {current!r} != {snapshots!r}")
