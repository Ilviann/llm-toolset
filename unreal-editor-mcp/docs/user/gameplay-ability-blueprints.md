# Gameplay Ability Blueprint inspection

> `UnrealMCPGAS` 0.4.0 exposes Gameplay Ability inspection through `asset_inspect`; it does not publish GAS authoring.

Install `UnrealMCP` and the optional `UnrealMCPGAS` 0.4.0 directory as separate project or Engine plugins, enable both plus the Engine Gameplay Ability System plugin, and restart Unreal Editor. The base and companion semantic versions are independent; both descriptors and binaries must agree on `companion_api_version: 2`, and the Python package must know schema revision 2.

An admitted `unreal-mcp-gas` companion publishes the exact inspection-only family `gameplay_ability`. A root `asset_inspect` call preserves the base asset identity and adds selector `gameplay_ability` plus one `gameplay_ability` block containing bounded `policies`, `tags`, `triggers`, and `effects` records. Calling the returned selector yields only that semantic selection. The companion contributes its typed fingerprint to the common snapshot, so repeat and selector calls can be compared safely.

Missing, disabled, mismatched, or unready GAS installations leave the neutral base response intact and expose no GAS block, selector, or mutation capability. This release cannot create, edit, compile, or save Gameplay Ability Blueprints through the companion family.

With `UE58` set to the Unreal Engine 5.8 installation root, package the companion offline from the same release state as the base:

```powershell
python scripts/package_plugin.py --target-platforms Win64 --gas-companion
```

Install the resulting base and companion packages together. On Windows, the graphical deployment helper can build and install both when its GAS checkbox is selected. Unreal MCP never downloads or enables GAS at runtime.

[Gameplay Effect status](gameplay-effects.md) · [Companion setup](companion-plugins.md) · [Asset inspection](asset-inspection.md) · [Limits](limits-and-testing.md)
