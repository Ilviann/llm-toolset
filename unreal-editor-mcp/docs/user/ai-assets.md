# AI asset inspection

Install and enable the base `UnrealMCP` plugin and optional `UnrealMCPAI` 0.1.0 companion, then restart the editor. `capabilities` must report `unreal-mcp-ai` with `effective_ready: true`, companion API v2/schema revision 2, and exactly eight inspection-only asset families.

For an independent Win64 package, run:

```powershell
python scripts\package_plugin.py --ai-companion --engine-root $env:UE58 --target-platforms Win64 --output ..\build\unreal-mcp-ai-ue58
```

Install the packaged companion beside a compatible base plugin and enable `UnrealMCPAI`. The companion carries its AI module dependencies; it does not add them to the base plugin. The Windows graphical deployment helper does not yet offer an AI checkbox, so use the independent package command.

## Requests and records

Use the shared tool with an exact project-content path:

```json
{"asset_path":"/Game/AI/BT_Guard.BT_Guard"}
```

Behavior Trees return static composite/task/decorator/service topology, ordering, policy, Blackboard selectors, class/asset references, editor comments/positions when available, and diagnostics. Blackboards return their bounded inheritance chain, ordered effective keys, type constraints, synchronization, duplicate/shadow state, and schema snapshot. Environment Queries return ordered options, generators, tests, contexts, scoring/filter policy, and fixed typed parameters.

Custom task, decorator, service, EQS generator, and EQS context Blueprints retain ordinary Blueprint inspection and add their native AI base, supported event/override points, selectors or contexts, and allowlisted inherited defaults. Unknown plugin subclasses stay visible as typed unsupported records.

Select one family record with, for example:

```json
{"asset_path":"/Game/AI/BB_Guard.BB_Guard","selector":"blackboard"}
```

Successful responses remain deterministic safe YAML. The companion never runs AI, reads live Blackboard values or debugger/PIE state, recursively traverses referenced assets, edits content, compiles, or saves.

[Tool overview](tool-guides.md) · [Companion setup](companion-plugins.md) · [Wire contracts](../types/ai-asset-inspection/index.md)
