# CommonUI Widget Blueprint inspection

> `UnrealMCPCommonUI` 0.3.0 exposes CommonUI root defaults and CommonUI widgets throughout supported Widget Blueprint trees through `asset_inspect`; it does not publish CommonUI authoring.

Install `UnrealMCP` and the optional `UnrealMCPCommonUI` 0.3.0 directory as separate project or Engine plugins, enable both plus Unreal Engine's `CommonUI` plugin, and restart Unreal Editor. The base and companion semantic versions are independent; both descriptors and binaries must agree on `companion_api_version: 2`, and the Python package must know schema revision 2. Unreal MCP never installs or enables CommonUI.

An admitted `unreal-mcp-commonui` companion publishes the exact inspection-only family `commonui_widget`. A root `asset_inspect` call preserves neutral/base UMG identity and adds a `commonui_widgets` collection to every supported Widget Blueprint. `UCommonUserWidget` roots also retain `commonui_widget`, `commonui_activation`, and `commonui_references`. Missing, disabled, mismatched, or unready CommonUI installations leave base UMG inspection intact and expose no CommonUI blocks, selectors, or mutation capability.

Page the allowlisted CommonUI children, then select one by its exact Widget Tree name:

```json
{"asset_path":"/Game/UI/WBP_Menu.WBP_Menu","selector":"commonui_widgets","page_size":10,"page_index":0}
```

```json
{"asset_path":"/Game/UI/WBP_Menu.WBP_Menu","selector":"commonui_widgets/ConfirmButton"}
```

Records identify the widget class and semantic family, local/inherited ownership, parent and children, plus exact CommonUI configuration. Supported categories include buttons; text, rich-text, numeric, and date/time text; action display; lazy image/widget; activatable containers and switchers; tab/list families; and carousels. Style classes, action/domain references, transition/navigation/selection policy, and input display/consumption settings are decoded without loading unresolved soft references.

With `UE58` set to the Unreal Engine 5.8 installation root, package the companion offline from the same release state as the base:

```powershell
python scripts/package_plugin.py --target-platforms Win64 --commonui-companion
```

Direct recursive inspection of CommonUI styles or Data Assets is not performed. Runtime activation, focus, selection, lazy-load state, input simulation, settings, and CommonUI-specific authoring are not exposed. A tree over 128 widgets, a CommonUI input-action array over 32 rows, or a response beyond the base limits fails closed.

[Companion setup](companion-plugins.md) · [Widget authoring](widget-blueprints.md) · [Asset inspection](asset-inspection.md) · [Limits](limits-and-testing.md)
