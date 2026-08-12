# Gameplay Ability Blueprint inspection

> Released in 0.30.0, this companion inspector is not model-facing in 0.36.0 after removal of its former shared Blueprint read route. Native registration remains available for a future redesigned read contract; `asset_inspect-core` intentionally does not expose GAS semantics.

Install `UnrealMCP` and the optional `UnrealMCPGAS` 0.2.1 directory as separate project or Engine plugins, enable both plus the Engine Gameplay Ability System plugin, and restart Unreal Editor. The base and companion semantic versions are independent; both descriptors and binaries must agree on `companion_api_version: 1`, and the Python package must know schema revision 1.

`capabilities` may still report the admitted `unreal-mcp-gas` companion and its native inspection feature flags. Those flags describe the unchanged API v1 native registration, not a callable model-facing read tool. `asset_inspect` returns only its bounded neutral core identity and limitations for Gameplay Ability assets. Do not rely on the dormant record, selector, or snapshot contracts for authoring until a replacement GAS read facade is approved.

With `UE58` set to the Unreal Engine 5.8 installation root, package the companion offline from the same release state as the base:

```powershell
python scripts/package_plugin.py --target-platforms Win64 --gas-companion
```

Install the resulting base and companion packages together. On Windows, the graphical deployment helper can build and install both when its GAS checkbox is selected. Unreal MCP never downloads or enables GAS at runtime.

[Gameplay Effect status](gameplay-effects.md) · [Companion setup](companion-plugins.md) · [Asset inspection](asset-inspection.md) · [Limits](limits-and-testing.md)
