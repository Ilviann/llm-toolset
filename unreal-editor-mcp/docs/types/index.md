# Types and function libraries

References in this directory are grouped by the component that owns the contract or reusable library. Each component gets a subdirectory with its own `index.md`; that index links only to the custom data types, wire records, collaborator protocols, and reusable function libraries immediately inside it.

- [`python/`](python/index.md) — Python project, discovery, error, schema, and bridge-client contracts.
- [`editor-bridge/`](editor-bridge/index.md) — native request, error, capability, state, discovery, and limit contracts.
- [`blueprint-family-policy/`](blueprint-family-policy/index.md) — supported Actor/GameMode/GameState/GameInstance classifications, operation matrix, live capabilities, result fields, and exclusions.
- [`blueprint-inspector/`](blueprint-inspector/index.md) — published-family Blueprint queries, pages, snapshots, identities, and bounded value encodings.
- [`blueprint-action-catalog/`](blueprint-action-catalog/index.md) — graph-action queries, records, opaque identities, limits, caching, and invalidation.
- [`blueprint-graph-editor/`](blueprint-graph-editor/index.md) — action-backed graph-node lifecycle, typed pin defaults, wildcard-aware connections, bounded conversions, persistent identities, and results.
- [`blueprint-mutator/`](blueprint-mutator/index.md) — published-family Blueprint creation, compile/save, components/defaults, member variables, functions, locals, macros, custom events, diagnostics, mutation scope, and cleanup contracts.
