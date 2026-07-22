# Architecture components

Each file in this directory documents one implemented cohesive component: what it owns, the source files inside it, its dependency direction, its invariants, and the checks to run when it changes.

- [`python-mcp-server.md`](python-mcp-server.md) — stdio protocol, schemas, discovery, authenticated bridge client, errors, and shutdown.
- [`editor-bridge.md`](editor-bridge.md) — plugin composition, credentials, listener/route ownership, dispatch, commands, limits, and heartbeat.
- [`blueprint-family-policy.md`](blueprint-family-policy.md) — explicit Actor/GameMode/GameState/GameInstance classification, published operation matrix, live family capabilities, and exclusions.
- [`blueprint-inspector.md`](blueprint-inspector.md) — bounded Asset Registry discovery, exact published-family Blueprint inspection, snapshots, identities, values, and cursors.
- [`blueprint-action-catalog.md`](blueprint-action-catalog.md) — bounded live graph-action discovery, filters, opaque identities, caching, and invalidation.
- [`blueprint-graph-editor.md`](blueprint-graph-editor.md) — transactional action-backed node lifecycle, typed pin defaults, wildcard-aware connections, bounded conversions, identity completion, and read-back.
- [`blueprint-mutator.md`](blueprint-mutator.md) — safe published-family Blueprint creation, compilation, package saving, diagnostics, cleanup, and read-back.
- [`gameplay-framework-editor.md`](gameplay-framework-editor.md) — narrow verified default GameMode/GameInstance project assignment and config restoration.
- [`automated-verification.md`](automated-verification.md) — Python, native, public-API-probe, and cross-process verification boundaries.
