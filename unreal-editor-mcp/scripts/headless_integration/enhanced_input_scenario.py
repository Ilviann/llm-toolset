"""Production-socket Enhanced Input asset inspection checks."""

from unreal_editor_mcp.bridge import UnrealBridge


FIXTURES = (
    ("/Game/UnrealMCPEnhancedInput/IA_InspectionFixture.IA_InspectionFixture", "input_action"),
    ("/Game/UnrealMCPEnhancedInput/IMC_InspectionFixture.IMC_InspectionFixture", "input_mapping_context"),
    ("/Game/UnrealMCPEnhancedInput/PMI_LegacyFixture.PMI_LegacyFixture", "player_mappable_input_config"),
    ("/Game/UnrealMCPEnhancedInput/BP_InputTriggerFixture.BP_InputTriggerFixture", "input_trigger_blueprint"),
    ("/Game/UnrealMCPEnhancedInput/BP_InputModifierFixture.BP_InputModifierFixture", "input_modifier_blueprint"),
)


def inspect_enhanced_input_fixtures(bridge: UnrealBridge) -> dict[str, str]:
    snapshots: dict[str, str] = {}
    for path, section in FIXTURES:
        first = bridge.call("asset_inspect", {"asset_path": path})
        repeated = bridge.call("asset_inspect", {"asset_path": path})
        if first != repeated or first.get("asset", {}).get("path") != path \
                or section not in first or section not in first.get("selectors", []) \
                or "limitations" in first:
            raise AssertionError(
                f"Enhanced Input root inspection changed for {path}: {first!r}"
            )
        selected = bridge.call("asset_inspect", {
            "asset_path": path, "selector": section,
        })
        if selected.get("snapshot_id") != first.get("snapshot_id") \
                or selected.get("selection", {}).get("selector") != section \
                or selected.get("asset", {}).get("path") != path \
                or section not in selected:
            raise AssertionError(
                f"Enhanced Input selector inspection changed for {path}: {selected!r}"
            )
        snapshots[path] = first["snapshot_id"]

    legacy = bridge.call("asset_inspect", {"asset_path": FIXTURES[2][0]})
    if legacy["player_mappable_input_config"].get("deprecated_in_ue_5_8") is not True:
        raise AssertionError(f"legacy Enhanced Input deprecation is missing: {legacy!r}")
    contexts = legacy["player_mappable_input_config"].get("contexts")
    if not isinstance(contexts, list) or len(contexts) != 1 \
            or contexts[0].get("context_path") != FIXTURES[1][0] \
            or contexts[0].get("priority") != 7:
        raise AssertionError(f"legacy Enhanced Input contexts changed: {legacy!r}")
    action = bridge.call("asset_inspect", {"asset_path": FIXTURES[0][0]})["input_action"]
    if action.get("player_mappable_settings", {}).get("name") != "Move":
        raise AssertionError(f"Input Action player-mappable metadata changed: {action!r}")
    mappings = bridge.call("asset_inspect", {"asset_path": FIXTURES[1][0]})[
        "input_mapping_context"
    ].get("mappings")
    if not isinstance(mappings, list) or len(mappings) != 2 \
            or mappings[0].get("key") != mappings[1].get("key") \
            or mappings[0].get("mapping_id") == mappings[1].get("mapping_id"):
        raise AssertionError(f"repeated ordered Enhanced Input mappings changed: {mappings!r}")
    return snapshots


def verify_restarted_enhanced_input(
    bridge: UnrealBridge, snapshots: dict[str, str],
) -> None:
    current = inspect_enhanced_input_fixtures(bridge)
    if current != snapshots:
        raise AssertionError(
            f"Enhanced Input snapshots changed after restart: {current!r} != {snapshots!r}"
        )
