# Types and function libraries

References in this directory are grouped by the component that owns the contract or reusable library. Each component gets a subdirectory with its own `index.md`; that index links only to the custom data types, wire records, collaborator protocols, and reusable function libraries immediately inside it.

- [`python/`](python/index.md) — Python project, discovery, error, schema, bridge-client, and editor-lifecycle contracts.
- [`editor-bridge/`](editor-bridge/index.md) — native request, error, capability, state, discovery, and limit contracts.
- [`blueprint-family-policy/`](blueprint-family-policy/index.md) — supported Actor/GameMode/GameState/GameInstance classifications, operation matrix, live capabilities, result fields, and exclusions.
- [`blueprint-inspector/`](blueprint-inspector/index.md) — published-family Blueprint queries, pages, snapshots, identities, and bounded value encodings.
- [`blueprint-action-catalog/`](blueprint-action-catalog/index.md) — graph-action queries, records, opaque identities, limits, caching, and invalidation.
- [`blueprint-graph-editor/`](blueprint-graph-editor/index.md) — action-backed graph-node lifecycle, typed pin defaults, wildcard-aware connections, bounded conversions, persistent identities, and results.
- [`blueprint-mutator/`](blueprint-mutator/index.md) — published-family Blueprint creation, compile/save, components/defaults, member variables, functions, locals, macros, custom events, diagnostics, mutation scope, and cleanup contracts.
- [`gameplay-framework-editor/`](gameplay-framework-editor/index.md) — exact default GameMode/GameInstance assignment, persistence, results, and exclusions.
- [`game-data-service/`](game-data-service/index.md) — user-defined struct schemas, Data Table rows, recursive reflected values, batching, dependencies, snapshots, and limits.
- [`level-service/`](level-service/index.md) — mounted map discovery, current-map identities, revisions, snapshots, cursors, and safe opening.
- [`asset-reference-service/`](asset-reference-service/index.md) — exact reference targets, evidence categories, scan completeness, snapshots, cursors, limits, and exclusions.
- [`windows-deployment-helper/`](windows-deployment-helper/index.md) — Windows project and Engine selection, binary-package filtering, replace-safe installation, and LM Studio JSON.
