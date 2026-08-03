# Gameplay Effect inspection

Install and enable the base `UnrealMCP` plugin, optional `UnrealMCPGAS` 0.2.0 companion, and Engine Gameplay Ability System plugin, then restart Unreal Editor. The base and companion versions are independent; companion API v1 and schema revision 1 must match the base and Python catalog exactly.

Call `capabilities` first. The `unreal-mcp-gas` companion must report `effective_ready: true`, `features.gas_gameplay_effects_inspection` must be true, and `features.gas_gameplay_effects_mutation` must be false. `blueprint_families` then includes inspection-only `gameplay_effect` alongside `gameplay_ability`.

Discover a narrow content path with the ordinary Blueprint tool:

```json
{"mode":"discover","package_path":"/Game/Effects","page_size":25}
```

Inspect one exact data-only Gameplay Effect:

```json
{
  "mode": "inspect",
  "asset_path": "/Game/Effects/GE_Burning.GE_Burning",
  "sections": ["summary", "gameplay_effect"],
  "include_inherited": true,
  "page_size": 100
}
```

The typed section returns fixed records for duration/period, modifiers and their four supported magnitude forms, executions, stacking/overflow, cues, tags, granted abilities, additional-effect chains, requirements, allowlisted Gameplay Effect Components, and cross-field relationships. Values distinguish local from inherited data. Class, attribute, tag, curve, ability, and effect references report resolution and compatibility; unsupported components or layouts are explicit rather than silently omitted.

All records share the standard Blueprint snapshot and cursor. Re-run inspection after any editor change. Cyclic, too-deep, or too-large effect chains are bounded and reported; no chained effect is applied or evaluated.

Existing Gameplay Ability records link cost and cooldown fields back through `class_path` and `asset_path`. For example, inspect the ability with sections `summary` and `gameplay_ability`, then inspect the returned effect asset with `summary` and `gameplay_effect`.

This release cannot create, edit, compile, or save Gameplay Effects. It also does not create tags, author Attribute Sets or Gameplay Cues, evaluate custom calculations, construct runtime specs, or touch Ability System Components. Those operations reject without modifying the package.

Package the companion offline with `python scripts/package_plugin.py --gas-companion`; install base and companion packages separately. The Windows graphical deployment helper does not deploy companions.

[Gameplay Ability inspection](gameplay-ability-blueprints.md) · [Companion setup](companion-plugins.md) · [Blueprint inspection](blueprint-inspection.md) · [Limits](limits-and-testing.md)
