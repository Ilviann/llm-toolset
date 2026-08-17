# Companion plugins

Companion plugins are optional trusted native editor plugins that extend existing Unreal MCP tool families for explicitly supported asset or component types. The runtime never downloads, installs, enables, or hot-loads a companion or its Engine dependencies; the local Windows deployment helper can perform an explicit user-selected offline installation.

## Install and enable

Install the base plugin and companion as separate plugin directories, then enable both for the project. The companion descriptor must depend on `UnrealMCP`, declare exact `companion_api_version: 2` and schema revision `2`, identify its extension under `unreal_mcp_companion`, and set its module `LoadingPhase` to `None` so only the initialized base registry loads it. Restart Unreal Editor after any enablement or binary change; live enablement and hot replacement are intentionally unsupported.

Base and companion semantic versions are independent. Compatibility depends on exact agreement among the base descriptor/compiled API version and the companion descriptor/compiled API version, plus the supported extension-schema revision. Required Engine plugins and modules must already be installed, enabled, and loaded.

## Detect availability

Call `capabilities` and inspect `companions`. `ready: true` means native registration succeeded. `asset_families` lists bounded API-v2 family identities, class policies, inspection/creation/editing support, selector routes, stable nested-identity kinds, persistence, and limits. `effective_ready: true` additionally means the exact Python package knows that extension/schema and accepts the expected family identities and operation shape. Unknown, incomplete, or malformed native extensions deliberately expose no model-facing operation schema or companion inspection block.

Readonly mode exposes admitted read contributions only. Start the Python server with explicit `--writable` access before mutation branches can appear in `tools/list` or dispatch. After lifecycle or capability changes the server emits `notifications/tools/list_changed`; clients should retrieve `tools/list` again.

## Packaging and troubleshooting

With `UE58` set to the Unreal Engine 5.8 installation root, authors can package the repository fixture independently with:

```powershell
python scripts/package_plugin.py --target-platforms Win64 --companion-fixture
```

Package the released `UnrealMCPGAS` 0.4.0 Gameplay Ability, Gameplay Effect, Cue Notify, Attribute Set, and calculation inspection companion with `--gas-companion`; it requires an API-v2-compatible base package. Package the released `UnrealMCPCommonUI` 0.3.0 Widget Blueprint inspection companion with `--commonui-companion`. Package the released `UnrealMCPEnhancedInput` 0.1.0 action, mapping, legacy-config, and trigger/modifier inspection companion with `--enhanced-input-companion`. Package the released `UnrealMCPAI` 0.1.0 Behavior Tree, Blackboard, EQS, and custom AI Blueprint inspection companion with `--ai-companion`. On Windows, the graphical deployment helper provides independent default-off GAS, CommonUI, and Enhanced Input companion checkboxes; package AI independently until a separately scoped deployment option is released.

Released companions use the same UAT `BuildPlugin` contract with their own descriptor, and install their compatible base package separately. The packaging wrapper restores and verifies source-owned descriptor fields that UAT may omit. If a companion is unavailable, check enablement and restart state, exact descriptor/compiled API/schema values, owning module identity and load phase, and required Engine plugin/module state. A companion must not expose runtime schemas, arbitrary property paths, listener settings, or credentials.

[Setup](setup-and-operation.md) · [Tool guides](tool-guides.md) · [Native author contract](../types/companion-extension-registry/index.md)
