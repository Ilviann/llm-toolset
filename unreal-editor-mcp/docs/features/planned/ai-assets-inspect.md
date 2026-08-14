---
feature_id: ai-assets-inspect
status: planned
depends_on:
  - asset-inspect-core
  - companion-asset-adapters
released_in: null
---

# `ai-assets-inspect` — Unreal AI asset inspection

**Outcome:** `asset_inspect` can explain the static design of Behavior Trees, Blackboards, Environment Queries, and their supported custom Blueprint node classes without running AI logic or returning debugger state.

**Depends on:**

- [`asset-inspect-core`](../completed/asset-inspect-core.md)
- [`companion-asset-adapters`](../completed/companion-asset-adapters.md)

### Asset-family scope

- Add an optional editor-only `UnrealMCPAI` companion that owns direct `AIModule` and required public editor-module dependencies. Reuse the released companion registry, `asset_inspect` tool, common target resolution, safe-YAML rendering, selectors, paging, snapshots, errors, limits, and base-only fallback; add no AI-specific model-facing tool.
- Inspect exact `UBehaviorTree` assets as stable composite/task/decorator/service topology with child ordering, execution and observer policy, abort/flow-control settings, subtree and external asset references, Blackboard key selectors, node identities, comments, positions, and structural diagnostics.
- Inspect exact `UBlackboardData` assets with parent inheritance, ordered keys, key IDs, key-type semantics, instance-synchronization policy, enum/class/object constraints, defaults where persisted, duplicate or shadowed names, references, and a deterministic schema snapshot.
- Inspect exact custom Blueprint assets derived from `UBTTask_BlueprintBase`, `UBTDecorator_BlueprintBase`, and `UBTService_BlueprintBase` by composing ordinary Blueprint members/graphs with AI event, condition, interval, flow-abort, and Blackboard-selector semantics. Connect Behavior Tree node records to their exact native or Blueprint implementation classes without executing them.
- Treat `UEnvQuery` as the initial additional AI data asset. Report options, generators, tests, contexts, scoring/filter policy, typed parameters, ordering, references, and bounded graph-like topology; compose ordinary Blueprint inspection for supported `UEnvQueryGenerator_BlueprintBase` and `UEnvQueryContext_BlueprintBase` descendants.
- Bound node counts, depth, subtree and parent traversal, Blackboard keys, query options/tests, class and asset resolution, diagnostics, response size, and Game-thread time. Return typed unsupported records for unknown node, key, generator, test, or context subclasses instead of using unrestricted reflection.

### Exclusions and completion gate

- Do not execute a Behavior Tree or EQS query; inspect live Blackboard values, active paths, debugger traces, perception, navigation meshes, runtime AI controllers, or PIE state; mutate assets; or expose arbitrary UObject calls. StateTree, Smart Objects, Mass AI, learning agents, and plugin-specific AI frameworks require separately named roadmap features.
- Verify representative native and Blueprint tasks, decorators, services, Blackboard inheritance and key types, Behavior Tree subtrees/cycles, EQS generators/tests/contexts, missing classes/references, unavailable companion states, bounds, deterministic output, restart stability, and non-mutation.
- Complete only after focused and full Python tests, UE 5.8 public-header probes and native Automation, production-socket headless integration, adaptive/forced-unity/non-unity editor builds, and base/AI Win64 packaging pass. Record remaining applicable macOS verification in the roadmap backlog.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
