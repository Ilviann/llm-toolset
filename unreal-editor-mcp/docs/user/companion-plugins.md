# Companion plugins

Companion plugins are optional trusted native editor plugins that extend existing Unreal MCP tool families for explicitly supported asset or component types. Unreal MCP never downloads, installs, enables, or hot-loads a companion or its Engine dependencies.

## Install and enable

Install the base plugin and companion as separate plugin directories, then enable both for the project. The companion descriptor must depend on `UnrealMCP`, declare exact `companion_api_version: 1`, and identify its extension under `unreal_mcp_companion`. Restart Unreal Editor after any enablement or binary change; live enablement and hot replacement are intentionally unsupported.

Base and companion semantic versions are independent. Compatibility depends on exact agreement among the base descriptor/compiled API version and the companion descriptor/compiled API version, plus the supported extension-schema revision. Required Engine plugins and modules must already be installed, enabled, and loaded.

## Detect availability

Call `capabilities` and inspect `companions`. `ready: true` means native registration succeeded. `effective_ready: true` additionally means the exact Python package knows that extension/schema. Unknown native extensions deliberately expose no model-facing operation schema.

Readonly mode exposes admitted read contributions only. Start the Python server with explicit `--writable` access before mutation branches can appear in `tools/list` or dispatch. After lifecycle or capability changes the server emits `notifications/tools/list_changed`; clients should retrieve `tools/list` again.

## Packaging and troubleshooting

Authors can package the repository fixture independently with:

```powershell
python scripts/package_plugin.py --engine-root "C:\Program Files\Epic Games\UE_5.8" --target-platforms Win64 --companion-fixture
```

Released companions use the same UAT `BuildPlugin` contract with their own descriptor, and install their compatible base package separately. If a companion is unavailable, check enablement and restart state, exact descriptor/compiled API/schema values, owning module identity, and required Engine plugin/module state. A companion must not expose runtime schemas, arbitrary property paths, listener settings, or credentials.

[Setup](setup-and-operation.md) · [Tool guides](tool-guides.md) · [Native author contract](../types/companion-extension-registry/index.md)
