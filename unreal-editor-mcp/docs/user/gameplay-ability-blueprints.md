# Gameplay Ability Blueprint inspection

Install `UnrealMCP` and the optional `UnrealMCPGAS` 0.2.0 directory as separate project or Engine plugins, enable both plus the Engine Gameplay Ability System plugin, and restart Unreal Editor. The base and companion semantic versions are independent; both descriptors and binaries must agree on `companion_api_version: 1`, and the Python package must know schema revision 1.

Check `capabilities` before using the family. `companions` must contain `extension_id: "unreal-mcp-gas"` with `effective_ready: true`; `features.gas_ability_blueprints_inspection` must be true and `features.gas_ability_blueprints_mutation` false. `blueprint_families` then contains an inspection-only `gameplay_ability` record.

Discover abilities with the ordinary tool:

```json
{"mode":"discover","package_path":"/Game/Abilities","page_size":25}
```

Inspect ordinary Blueprint structure plus the typed GAS section:

```json
{
  "mode": "inspect",
  "asset_path": "/Game/Abilities/GA_Dash.GA_Dash",
  "sections": ["summary", "parent_class", "variables", "graphs", "nodes", "gameplay_ability"],
  "include_inherited": true,
  "page_size": 100
}
```

The typed records cover policies, all supported tag containers, triggers, and cost/cooldown Gameplay Effect class references. Fields identify local versus inherited values; nested triggers have stable identities; bounded collections report total counts and truncation. All records share the ordinary authoritative `snapshot_id` and cursor contract.

With `UE57` set to the Unreal Engine 5.7 installation root, package the companion offline from the same release state as the base:

```powershell
python scripts/package_plugin.py --target-platforms Win64 --gas-companion
```

Install the resulting base and companion packages together. On Windows, the graphical deployment helper can build and install both when its GAS checkbox is selected. Unreal MCP never downloads or enables GAS at runtime.

[Gameplay Effect inspection](gameplay-effects.md) · [Companion setup](companion-plugins.md) · [Blueprint inspection](blueprint-inspection.md) · [Limits](limits-and-testing.md)
