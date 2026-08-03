# Architecture components

Each file in this directory documents one implemented cohesive component: what it owns, the source files inside it, its dependency direction, its invariants, and the checks to run when it changes.

- [`python-mcp-server.md`](python-mcp-server.md) — stdio protocol, schemas, discovery, authenticated bridge client, errors, and shutdown.
- [`editor-bridge.md`](editor-bridge.md) — plugin composition, credentials, listener/route ownership, dispatch, commands, limits, and heartbeat.
- [`companion-extension-registry.md`](companion-extension-registry.md) — companion discovery, admission, lifecycle, capability intersection, and base-owned dispatch policy.
- [`gas-ability-inspection.md`](gas-ability-inspection.md) — optional GAS companion ownership, Gameplay Ability family classification, typed inspection, fingerprints, and read-only capability policy.
- [`editor-lifecycle.md`](editor-lifecycle.md) — opt-in configured launch, graceful shutdown, durable restart, cancellation, and retained records.
- [`blueprint-family-policy.md`](blueprint-family-policy.md) — explicit Actor/GameMode/GameState/GameInstance/Widget classification, published operation matrix, live family capabilities, and exclusions.
- [`blueprint-inspector.md`](blueprint-inspector.md) — bounded Asset Registry discovery, exact published-family Blueprint inspection, snapshots, identities, values, and cursors.
- [`blueprint-action-catalog.md`](blueprint-action-catalog.md) — bounded live graph-action discovery, filters, opaque identities, caching, and invalidation.
- [`blueprint-graph-editor.md`](blueprint-graph-editor.md) — request decoding, focused node/pin operation handlers, transactional graph editing, identity completion, and result read-back.
- [`blueprint-block-replacement.md`](blueprint-block-replacement.md) — complete user-function boundaries, scratch compilation, semantic parity, transactional apply, and rollback.
- [`blueprint-mutator.md`](blueprint-mutator.md) — safe published-family Blueprint creation, compilation, package saving, diagnostics, cleanup, and read-back.
- [`widget-tree-service.md`](widget-tree-service.md) — bounded Widget Blueprint hierarchy/default inspection and stale-safe structural editing.
- [`umg-layout-service.md`](umg-layout-service.md) — typed common panel-slot layout editing and inspection.
- [`umg-style-service.md`](umg-style-service.md) — bounded reflected widget presentation editing.
- [`umg-binding-service.md`](umg-binding-service.md) — exact property bindings and Designer-event graph nodes.
- [`umg-authoring-support.md`](umg-authoring-support.md) — shared request validation, identity resolution, value decoding, and mutation results.
- [`gameplay-framework-editor.md`](gameplay-framework-editor.md) — narrow verified default GameMode/GameInstance project assignment and config restoration.
- [`game-data-service.md`](game-data-service.md) — request validation, struct/table operation handlers, bounded inspection/result building, transactions, saving, and cursors.
- [`level-service.md`](level-service.md) — bounded mounted-map discovery, current-map identities and snapshots, delegate invalidation, and safe ledger-backed opening.
- [`level-management-service.md`](level-management-service.md) — exact map creation/configuration, bounded World Settings, persistence/reload verification, and map-closure deletion coordination.
- [`level-actor-inspector.md`](level-actor-inspector.md) — descriptor-only actor queries, exact targeted loading, stable component identities, and requested reflected values.
- [`level-actor-editing-service.md`](level-actor-editing-service.md) — stale-safe actor batches, scoped World Partition mutation, transactions, affected packages, and verified saving.
- [`asset-reference-service.md`](asset-reference-service.md) — request validation and facade composition for reference capture and pagination.
- [`asset-reference-target-resolver.md`](asset-reference-target-resolver.md) — exact mounted-target validation, non-loading resolution, and metadata.
- [`asset-reference-registry-scanner.md`](asset-reference-registry-scanner.md) — bounded serialized, management, and searchable-name inbound evidence.
- [`asset-reference-live-scanner.md`](asset-reference-live-scanner.md) — bounded open-editor and direct loaded-object evidence.
- [`asset-reference-snapshot-builder.md`](asset-reference-snapshot-builder.md) — scan aggregation, stable ordering, and deterministic snapshot identity.
- [`asset-reference-cursor-store.md`](asset-reference-cursor-store.md) — registry serials, bounded snapshot retention, and stale-safe single-use pagination.
- [`asset-deletion-service.md`](asset-deletion-service.md) — exact ordinary-asset and bounded map-closure deletion, conservative preflight, retained outcomes, and persistence verification.
- [`windows-deployment-helper.md`](windows-deployment-helper.md) — tkinter project/Engine selection, installed Win64 packaging, optional matching-PDB deployment, and LM Studio configuration.
- [`automated-verification.md`](automated-verification.md) — Python, native, public-API-probe, and cross-process verification boundaries.
