"""Fixed repository-owned Unreal plugin identities and dependencies."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


APPLICATION_ROOT = Path(__file__).resolve().parents[2]
WORKSPACE_ROOT = APPLICATION_ROOT.parent


@dataclass(frozen=True)
class PluginIdentity:
    name: str
    descriptor: Path
    dependencies: tuple[str, ...] = ()


BASE_PLUGIN = PluginIdentity(
    "UnrealMCP",
    APPLICATION_ROOT / "plugin" / "UnrealMCP" / "UnrealMCP.uplugin",
)
FIXTURE_PLUGIN = PluginIdentity(
    "UnrealMCPTestCompanion",
    APPLICATION_ROOT
    / "plugin"
    / "UnrealMCPTestCompanion"
    / "UnrealMCPTestCompanion.uplugin",
    (BASE_PLUGIN.name,),
)
GAS_PLUGIN = PluginIdentity(
    "UnrealMCPGAS",
    APPLICATION_ROOT / "plugin" / "UnrealMCPGAS" / "UnrealMCPGAS.uplugin",
    (BASE_PLUGIN.name,),
)
COMMONUI_PLUGIN = PluginIdentity(
    "UnrealMCPCommonUI",
    APPLICATION_ROOT / "plugin" / "UnrealMCPCommonUI" / "UnrealMCPCommonUI.uplugin",
    (BASE_PLUGIN.name,),
)
ENHANCED_INPUT_PLUGIN = PluginIdentity(
    "UnrealMCPEnhancedInput",
    APPLICATION_ROOT
    / "plugin"
    / "UnrealMCPEnhancedInput"
    / "UnrealMCPEnhancedInput.uplugin",
    (BASE_PLUGIN.name,),
)
AI_PLUGIN = PluginIdentity(
    "UnrealMCPAI",
    APPLICATION_ROOT / "plugin" / "UnrealMCPAI" / "UnrealMCPAI.uplugin",
    (BASE_PLUGIN.name,),
)
PLUGINS = {
    plugin.name: plugin
    for plugin in (
        BASE_PLUGIN, FIXTURE_PLUGIN, GAS_PLUGIN, COMMONUI_PLUGIN,
        ENHANCED_INPUT_PLUGIN, AI_PLUGIN,
    )
}
