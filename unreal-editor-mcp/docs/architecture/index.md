# Architecture components

Each file in this directory documents one implemented cohesive component: what it owns, the source files inside it, its dependency direction, its invariants, and the checks to run when it changes.

- [`python-mcp-server.md`](python-mcp-server.md) — stdio protocol, schemas, discovery, authenticated bridge client, errors, and shutdown.
- [`editor-bridge.md`](editor-bridge.md) — plugin composition, credentials, listener/route ownership, dispatch, commands, limits, and heartbeat.
- [`editor-lifecycle.md`](editor-lifecycle.md) — opt-in configured launch, graceful shutdown, durable restart, cancellation, and retained records.
- [`blueprint-family-policy.md`](blueprint-family-policy.md) — explicit Actor/GameMode/GameState/GameInstance/Widget classification, published operation matrix, live family capabilities, and exclusions.
- [`blueprint-inspector.md`](blueprint-inspector.md) — bounded Asset Registry discovery, exact published-family Blueprint inspection, snapshots, identities, values, and cursors.
- [`blueprint-action-catalog.md`](blueprint-action-catalog.md) — bounded live graph-action discovery, filters, opaque identities, caching, and invalidation.
- [`blueprint-graph-editor.md`](blueprint-graph-editor.md) — request decoding, focused node/pin operation handlers, transactional graph editing, identity completion, and result read-back.
- [`blueprint-mutator.md`](blueprint-mutator.md) — safe published-family Blueprint creation, compilation, package saving, diagnostics, cleanup, and read-back.
- [`widget-tree-service.md`](widget-tree-service.md) — bounded Widget Blueprint hierarchy/default inspection and stale-safe structural editing.
- [`gameplay-framework-editor.md`](gameplay-framework-editor.md) — narrow verified default GameMode/GameInstance project assignment and config restoration.
- [`game-data-service.md`](game-data-service.md) — request validation, struct/table operation handlers, bounded inspection/result building, transactions, saving, and cursors.
- [`level-service.md`](level-service.md) — bounded mounted-map discovery, current-map identities and snapshots, delegate invalidation, and safe ledger-backed opening.
- [`asset-reference-service.md`](asset-reference-service.md) — request validation and facade composition for reference capture and pagination.
- [`asset-reference-target-resolver.md`](asset-reference-target-resolver.md) — exact mounted-target validation, non-loading resolution, and metadata.
- [`asset-reference-registry-scanner.md`](asset-reference-registry-scanner.md) — bounded serialized, management, and searchable-name inbound evidence.
- [`asset-reference-live-scanner.md`](asset-reference-live-scanner.md) — bounded open-editor and direct loaded-object evidence.
- [`asset-reference-snapshot-builder.md`](asset-reference-snapshot-builder.md) — scan aggregation, stable ordering, and deterministic snapshot identity.
- [`asset-reference-cursor-store.md`](asset-reference-cursor-store.md) — registry serials, bounded snapshot retention, and stale-safe single-use pagination.
- [`asset-deletion-service.md`](asset-deletion-service.md) — exact single-package deletion, conservative preflight, retained outcomes, and persistence verification.
- [`windows-deployment-helper.md`](windows-deployment-helper.md) — tkinter project/Engine selection, installed Win64 packaging, symbol-free project deployment, and LM Studio configuration.
- [`automated-verification.md`](automated-verification.md) — Python, native, public-API-probe, and cross-process verification boundaries.
