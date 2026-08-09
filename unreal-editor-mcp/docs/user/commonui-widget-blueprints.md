# CommonUI Widget Blueprint inspection

Install `UnrealMCP` and the optional `UnrealMCPCommonUI` 0.1.0 directory as separate project or Engine plugins, enable both plus Unreal Engine's `CommonUI` plugin, and restart Unreal Editor. The base and companion semantic versions are independent; both descriptors and binaries must agree on `companion_api_version: 1`, and the Python package must know schema revision 1. Unreal MCP never installs or enables CommonUI.

Call `capabilities` first. `companions` must contain `extension_id: "unreal-mcp-commonui"` with `effective_ready: true`; `features.commonui_widget_blueprints_inspection` must be true and `features.commonui_widget_blueprints_mutation` false. The native contribution advertises the `commonui_widget` inspection subfamily while ordinary discovery and result classification remain the published `widget` family.

Discover candidate Widget Blueprints with the ordinary tool:

```json
{"mode":"discover","package_path":"/Game/UI","family":"widget","page_size":25}
```

Inspect ordinary Blueprint/Widget content and all CommonUI sections together:

```json
{
  "mode": "inspect",
  "asset_path": "/Game/UI/WBP_MainMenu.WBP_MainMenu",
  "sections": [
    "summary", "parent_class", "widget_tree", "widget_defaults",
    "commonui_widget", "commonui_activation", "commonui_references"
  ],
  "include_inherited": true,
  "page_size": 100
}
```

The records report local versus inherited CommonUI defaults. Hard and soft references include stable identities and paths; `resolved: false` is expected for an unloaded or missing soft target and does not cause the companion to load it. A `UCommonUserWidget` that is not activatable returns `not_activatable_widget` for the activation/reference sections. All typed state participates in the ordinary `snapshot_id` and stale-page contract.

With `UE58` set to the Unreal Engine 5.8 installation root, package the companion offline from the same release state as the base:

```powershell
python scripts/package_plugin.py --target-platforms Win64 --commonui-companion
```

Only `UCommonUserWidget`-derived Widget Blueprints are supported in 0.1.0. CommonUI styles, Data Assets, settings, runtime behavior, input simulation, and CommonUI-specific authoring are not exposed.

[Companion setup](companion-plugins.md) · [Widget authoring](widget-blueprints.md) · [Blueprint inspection](blueprint-inspection.md) · [Limits](limits-and-testing.md)
