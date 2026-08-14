# CommonUI Widget Blueprint inspection

> Released in 0.35.0, this companion inspector is not model-facing in 0.36.0 after removal of its former shared Blueprint read route. Native registration remains available for a future redesigned read contract; `asset_inspect-core` intentionally does not expose CommonUI semantics.

Install `UnrealMCP` and the optional `UnrealMCPCommonUI` 0.1.1 directory as separate project or Engine plugins, enable both plus Unreal Engine's `CommonUI` plugin, and restart Unreal Editor. The base and companion semantic versions are independent; both descriptors and binaries must agree on `companion_api_version: 2`, and the Python package must know schema revision 2. Unreal MCP never installs or enables CommonUI.

`capabilities` may still report the admitted `unreal-mcp-commonui` companion and its native inspection feature flags. Those flags describe its migrated JSON-neutral API-v2 registration, not a callable model-facing read tool. Core `asset_inspect` returns only the bounded neutral Widget Blueprint identity and limitations; base UMG and CommonUI semantic read-back require `companion-asset-adapters`.

With `UE58` set to the Unreal Engine 5.8 installation root, package the companion offline from the same release state as the base:

```powershell
python scripts/package_plugin.py --target-platforms Win64 --commonui-companion
```

Only `UCommonUserWidget`-derived Widget Blueprints are supported in 0.1.1. CommonUI styles, Data Assets, settings, runtime behavior, input simulation, and CommonUI-specific authoring are not exposed.

[Companion setup](companion-plugins.md) · [Widget authoring](widget-blueprints.md) · [Asset inspection](asset-inspection.md) · [Limits](limits-and-testing.md)
