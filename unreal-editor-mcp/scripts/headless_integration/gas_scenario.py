"""Unified GAS asset-family adapter checks."""

from unreal_editor_mcp.bridge import UnrealBridge


def verify_gas_inspection(bridge: UnrealBridge) -> None:
    fixtures = (
        ("/Game/UnrealMCPGAS/GE_InspectionFixture.GE_InspectionFixture", "gameplay_effect"),
        ("/Game/UnrealMCPGAS/GA_EffectReferenceFixture.GA_EffectReferenceFixture", "gameplay_ability"),
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
