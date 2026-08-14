# CommonUI Widget Blueprint inspection

> `UnrealMCPCommonUI` 0.2.0 exposes CommonUI Widget Blueprint inspection through `asset_inspect`; it does not publish CommonUI authoring.

Install `UnrealMCP` and the optional `UnrealMCPCommonUI` 0.2.0 directory as separate project or Engine plugins, enable both plus Unreal Engine's `CommonUI` plugin, and restart Unreal Editor. The base and companion semantic versions are independent; both descriptors and binaries must agree on `companion_api_version: 2`, and the Python package must know schema revision 2. Unreal MCP never installs or enables CommonUI.

An admitted `unreal-mcp-commonui` companion publishes the exact inspection-only family `commonui_widget`. A root `asset_inspect` call preserves the neutral Widget Blueprint identity and adds the bounded `commonui_widget`, `commonui_activation`, and `commonui_references` blocks with matching selectors. The companion fingerprint participates in the common snapshot. Missing, disabled, mismatched, or unready CommonUI installations leave neutral identity intact and expose none of those blocks, selectors, or mutation capability.

With `UE58` set to the Unreal Engine 5.8 installation root, package the companion offline from the same release state as the base:

```powershell
python scripts/package_plugin.py --target-platforms Win64 --commonui-companion
```

Only `UCommonUserWidget`-derived Widget Blueprints are supported in 0.2.0. CommonUI styles, Data Assets, settings, runtime behavior, input simulation, and CommonUI-specific authoring are not exposed.

[Companion setup](companion-plugins.md) · [Widget authoring](widget-blueprints.md) · [Asset inspection](asset-inspection.md) · [Limits](limits-and-testing.md)
