"""Unified GAS asset-family adapter checks."""

from unreal_editor_mcp.bridge import UnrealBridge


def verify_gas_inspection(bridge: UnrealBridge) -> None:
    fixtures = (
        ("/Game/UnrealMCPGAS/GE_InspectionFixture.GE_InspectionFixture", "gameplay_effect"),
        ("/Game/UnrealMCPGAS/GA_EffectReferenceFixture.GA_EffectReferenceFixture", "gameplay_ability"),
        ("/Game/UnrealMCPGAS/GCN_StaticFixture.GCN_StaticFixture", "gameplay_cue_notify"),
        ("/Game/UnrealMCPGAS/GCN_ActorFixture.GCN_ActorFixture", "gameplay_cue_notify"),
        ("/Game/UnrealMCPGAS/AS_InspectionFixture.AS_InspectionFixture", "attribute_set"),
        ("/Game/UnrealMCPGAS/MMC_InspectionFixture.MMC_InspectionFixture", "gameplay_mod_magnitude_calculation"),
        ("/Game/UnrealMCPGAS/Exec_InspectionFixture.Exec_InspectionFixture", "gameplay_effect_execution_calculation"),
    )
    for path, section in fixtures:
        first = bridge.call("asset_inspect", {"asset_path": path})
        repeated = bridge.call("asset_inspect", {"asset_path": path})
        if first != repeated or first.get("asset", {}).get("path") != path \
                or section not in first or section not in first.get("selectors", []) \
                or "limitations" in first:
            raise AssertionError(f"GAS adapter root inspection changed for {path}: {first!r}")
        selected = bridge.call("asset_inspect", {"asset_path": path, "selector": section})
        if selected.get("snapshot_id") != first.get("snapshot_id") \
                or selected.get("selection", {}).get("selector") != section \
                or selected.get("asset", {}).get("path") != path \
                or section not in selected:
            raise AssertionError(f"GAS adapter selector inspection changed for {path}: {selected!r}")
