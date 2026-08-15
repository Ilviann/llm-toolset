# Supporting Gameplay Ability System asset inspection

> `UnrealMCPGAS` 0.4.0 inspects Cue Notify, Attribute Set, magnitude-calculation, and execution-calculation Blueprint assets through `asset_inspect`; it does not publish GAS authoring or runtime execution.

Install and enable the base `UnrealMCP` plugin, optional `UnrealMCPGAS` 0.4.0 companion, and Engine Gameplay Ability System plugin, then restart Unreal Editor. The base and companion semantic versions are independent; companion API v2 and schema revision 2 must match the base and Python catalog exactly.

Call `asset_inspect` with one exact project asset path. Supported Cue Notify Static and Actor descendants add selector `gameplay_cue_notify`; Attribute Sets add `attribute_set`; magnitude and execution calculations add `gameplay_mod_magnitude_calculation` or `gameplay_effect_execution_calculation`. A root call also retains ordinary Blueprint members, defaults, graphs, diagnostics, and references.

Example request:

```json
{"asset_path":"/Game/Abilities/Attributes/AS_Combat.AS_Combat","selector":"attribute_set"}
```

Attribute records report declared and inherited identities, default base/current values, exact replication/RepNotify policy, and explicit clamp metadata. Cue records report tags, event response and lifecycle policy, allowlisted persisted placement/effect settings, and bounded references. Calculation records report captured attributes and their source/snapshot policy plus the supported magnitude or execution policy.

Missing, disabled, mismatched, or unready GAS installations expose none of these blocks or selectors and leave the base Blueprint result available. Ability Tasks remain nodes inside Gameplay Ability graphs rather than standalone assets. Inspection never dispatches cues, runs calculations, reads live Attribute Sets or Ability System Components, creates assets, compiles, saves, or mutates packages.

Package the companion offline with `python scripts/package_plugin.py --gas-companion`; on Windows, the graphical deployment helper can build and install it with a compatible base package.

[Gameplay Ability inspection](gameplay-ability-blueprints.md) · [Gameplay Effect inspection](gameplay-effects.md) · [Companion setup](companion-plugins.md) · [Asset inspection](asset-inspection.md)
