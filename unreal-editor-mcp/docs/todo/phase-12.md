# Phase 12 — Wildcards, conversions, and complete atomic graph editing

**Outcome:** Agents can complete Actor Blueprint logic through wildcard-aware graph editing and explicitly requested bounded conversion insertion.

### Implementation

- Extend `blueprint_graph_edit` connection preflight and verification with live-schema wildcard specialization and node or pin reconstruction.
- Add explicit opt-in automatic conversion while keeping it disabled by default.
- Bound inserted conversion nodes, include all insertions in preflight and the transaction, and return every inserted or reconstructed node and pin identity.
- Preserve the operation, snapshot, identity, protected-target, transaction, rollback, and limits contracts established in Phases 10 and 11.
- Audit the complete graph-edit tool schema and change records for consistent operation discriminators, concise results, and bounded context use.

### Verification

- Test wildcard specialization, conversion disabled and enabled, conversion limits, incompatible conversions, inserted-node identities, reconstruction, cycles, stale identities, rollback, and read-back.
- Repeat undo/redo, compile, save, restart, reload, lost-response reconciliation, and exact rejection-preservation tests across every graph-edit operation family.
- Implement, compile, save, restart, and inspect a small BeginPlay-driven Actor behavior using components, variables, direct links, wildcards, and an explicit conversion.

### Documentation and completion gate

- Add complete atomic graph-editing recipes, wildcard and conversion policy, operation reconciliation, and re-inspection guidance.
- Complete the phase only when the full BeginPlay acceptance workflow succeeds through MCP calls alone.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
