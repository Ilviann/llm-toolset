---
feature_id: ai-assets-inspect
status: completed
depends_on:
  - asset-inspect-core
  - companion-asset-adapters
released_in: "0.53.0"
---

# `ai-assets-inspect` — Unreal AI asset inspection

**Outcome:** `asset_inspect` explains the static design of Behavior Trees, Blackboards, Environment Queries, and supported custom AI Blueprint node classes without running AI logic or returning debugger state.

**Implementation status:** Completed in 0.53.0 with the independent `UnrealMCPAI` 0.1.0 companion on unchanged companion API v2 and schema revision 2. Eight read-only families compose with neutral or ordinary Blueprint inspection, while an absent or rejected companion leaves the base contract unchanged.

**Depends on:**

- [`asset-inspect-core`](asset-inspect-core.md)
- [`companion-asset-adapters`](companion-asset-adapters.md)

### Released inspection contract

- `behavior_tree` reports bounded composite, task, decorator, and service topology; authored child order; stable node identities; public editor positions/comments when present; Blackboard selectors; fixed flow, observer, abort, subtree, and external-asset semantics; and structural diagnostics.
- `blackboard` reports a bounded parent chain, inherited and local ordered keys, key IDs, type-specific constraints, synchronization, shadows/duplicates, references, and a deterministic schema snapshot.
- `environment_query` reports ordered options, generators, tests, contexts, scoring/filter policy, typed fixed parameters, references, and support classification.
- `bt_task_blueprint`, `bt_decorator_blueprint`, `bt_service_blueprint`, `eqs_generator_blueprint`, and `eqs_context_blueprint` compose ordinary Blueprint records with generated/native class identity, supported AI event/override points, Blackboard selectors or contexts, and allowlisted base defaults.
- Unknown plugin node, key, generator, test, and context subclasses remain typed unsupported records. Callers cannot select arbitrary reflected fields.

### Bounds and exclusions

Behavior Trees are limited to 512 nodes and depth 32; Blackboards to 256 keys and parent depth 8; EQS to 64 options and 256 tests; each object to 16 selectors or contexts, 64 diagnostics, 64 allowlisted properties, and 4,096 encoded bytes per property. The shared 65,536 value-node and four-megabyte document ceilings also apply; overflow fails closed.

The companion does not execute Behavior Trees or EQS, inspect live Blackboard values or active/debug paths, traverse referenced assets, query AI controllers, perception, navigation, or PIE state, mutate assets, call arbitrary UObjects, or cover StateTree, Smart Objects, Mass AI, Learning Agents, or plugin-specific AI frameworks.

### Verification

UE 5.8 public headers establish the asset, node, graph, key, query, generator, test, and Blueprint boundaries. Native Automation covers all eight families, deterministic snapshots, bounds, non-mutation, and persistent fixtures. Python catalog/contract tests cover exact admission, schemas, and packaging. Windows adaptive, forced-unity, non-unity, production-socket restart, full Automation, and isolated base/AI packaging gates complete the release. macOS remains tracked as preferred non-blocking follow-up.

[Back to roadmap](../../../ROADMAP.md) · [Wire contracts](../../types/ai-asset-inspection/index.md) · [User guide](../../user/ai-assets.md)
