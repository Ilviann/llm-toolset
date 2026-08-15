# Gameplay Effect inspection

> `UnrealMCPGAS` 0.4.0 exposes Gameplay Effect inspection through `asset_inspect`; it does not publish GAS authoring.

Install and enable the base `UnrealMCP` plugin, optional `UnrealMCPGAS` 0.4.0 companion, and Engine Gameplay Ability System plugin, then restart Unreal Editor. The base and companion versions are independent; companion API v2 and schema revision 2 must match the base and Python catalog exactly.

An admitted `unreal-mcp-gas` companion publishes the exact inspection-only family `gameplay_effect`. A root `asset_inspect` call preserves base identity and adds selector and block `gameplay_effect`. The block groups the eleven bounded typed collectors under `duration`, `modifiers`, `executions`, `stacking`, `cues`, `tags`, `components`, `granted_abilities`, `additional_effects`, `requirements`, and `relationships`. The companion fingerprint participates in the common snapshot.

Missing, disabled, mismatched, or unready GAS installations expose no Gameplay Effect block, selector, or mutation capability and leave the neutral base result available.

This release cannot create, edit, compile, or save Gameplay Effects. It also does not create tags, author Attribute Sets or Gameplay Cues, evaluate custom calculations, construct runtime specs, or touch Ability System Components. Those operations reject without modifying the package.

Package the companion offline with `python scripts/package_plugin.py --gas-companion`; install it with a compatible base package. On Windows, the graphical deployment helper can build, verify, and install both when its GAS checkbox is selected.

[Gameplay Ability status](gameplay-ability-blueprints.md) · [Companion setup](companion-plugins.md) · [Asset inspection](asset-inspection.md) · [Limits](limits-and-testing.md)
