# Enhanced Input asset inspection

Install and enable the base `UnrealMCP` plugin, Engine `EnhancedInput` plugin, and optional `UnrealMCPEnhancedInput` 0.1.0 companion, then restart the editor. `capabilities` must report `unreal-mcp-enhanced-input` with `effective_ready: true`, companion API v2/schema revision 2, and exactly five inspection-only asset families.

For an independent Win64 package, run:

```powershell
python scripts\package_plugin.py --enhanced-input-companion --engine-root $env:UE58 --target-platforms Win64 --output ..\build\unreal-mcp-enhanced-input-ue58
```

Install the packaged companion beside a compatible base plugin and enable both `UnrealMCPEnhancedInput` and Engine `EnhancedInput` in the project. On Windows, the graphical deployment helper can build, install, and enable the base and Enhanced Input companion together through its independent default-off checkbox.

## Requests and records

Use the shared tool with an exact project-content path:

```json
{"asset_path":"/Game/Input/IA_Move.IA_Move"}
```

Input Actions return value/accumulation and consumption policy, player-mappable settings, triggers, and modifiers. Mapping Contexts return default and override-profile mappings in authored order, including repeated keys, action references, effective player-mappable metadata, triggers, and modifiers. Legacy player-mappable configs remain readable but are explicitly deprecated for UE 5.8 migration work.

Custom trigger and modifier Blueprints retain ordinary Blueprint member/default/function/event/graph inspection and add their Enhanced Input base, supported override points, and persisted base settings. Inline unknown plugin subclasses remain explicit unsupported records.

Select one family record with, for example:

```json
{"asset_path":"/Game/Input/IMC_Player.IMC_Player","selector":"input_mapping_context"}
```

Successful responses remain deterministic safe YAML. The companion never injects input, evaluates trigger/modifier logic, reads per-player mappings, edits project settings, creates assets, compiles, or saves.

[Tool overview](tool-guides.md) · [Companion setup](companion-plugins.md) · [Wire contracts](../types/enhanced-input-asset-inspection/index.md)
