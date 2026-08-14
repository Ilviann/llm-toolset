# Gameplay Effect inspection

> Released in 0.31.0, this companion inspector is not model-facing in 0.36.0 after removal of its former shared Blueprint read route. Native registration remains available for a future redesigned read contract; `asset_inspect-core` intentionally does not expose GAS semantics.

Install and enable the base `UnrealMCP` plugin, optional `UnrealMCPGAS` 0.2.2 companion, and Engine Gameplay Ability System plugin, then restart Unreal Editor. The base and companion versions are independent; companion API v2 and schema revision 2 must match the base and Python catalog exactly.

`capabilities` may still report the admitted `unreal-mcp-gas` companion and its native Gameplay Effect inspection feature flag. That flag describes its migrated JSON-neutral API-v2 registration, not a callable model-facing read tool. `asset_inspect` returns only its bounded neutral core identity and limitations for Gameplay Effect assets. Do not use the dormant typed sections or snapshots as authoring preconditions until `companion-asset-adapters` supplies the unified read facade.

This release cannot create, edit, compile, or save Gameplay Effects. It also does not create tags, author Attribute Sets or Gameplay Cues, evaluate custom calculations, construct runtime specs, or touch Ability System Components. Those operations reject without modifying the package.

Package the companion offline with `python scripts/package_plugin.py --gas-companion`; install it with a compatible base package. On Windows, the graphical deployment helper can build, verify, and install both when its GAS checkbox is selected.

[Gameplay Ability status](gameplay-ability-blueprints.md) · [Companion setup](companion-plugins.md) · [Asset inspection](asset-inspection.md) · [Limits](limits-and-testing.md)
