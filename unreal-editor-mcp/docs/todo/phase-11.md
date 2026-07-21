# Phase 11 — Graph-node lifecycle

**Outcome:** Agents can create, move, and remove supported graph nodes through individually prevalidated and reconcilable mutations.

### Implementation

- Add `blueprint_graph_edit` operations for `add_node`, `move_node`, and `remove_node`. Add rename or comment operations only where public semantics and preservation behavior are proven.
- Require a valid retained action ID for `add_node`. Re-resolve and re-filter its rebuildable signature against the live graph before invoking the spawner; do not trust cached UObject pointers.
- Require operation ID, Blueprint snapshot, graph identity, and node identity preconditions as applicable. Reject read-only, inherited, interface, intermediate, construction, signature, or other protected targets unless the specific operation is proven safe.
- Assign persistent identities to created nodes and pins, detect spawners that return an existing unique node, and return a concise complete change record after reconstruction.
- Prevalidate node creation, position, ownership, deletion safety, and structure limits before opening one transaction. Verify postconditions and restore explicitly on unexpected failure.
- Bound graph size, position, result size, transaction work, diagnostics, retained operation state, and Game-thread duration.

### Verification

- Test event, function, macro, pure and impure, variable, and unique-event nodes; invalid action IDs; spawner failure; returned-existing-node behavior; protected-node deletion; move bounds; and stale identities.
- Test undo/redo, compilation, saving, restart, reload, and lost-response reconciliation for every node-lifecycle operation.
- Capture the graph before every rejection and injected failure and prove equality of structure, dirty state, compile state, and transaction history afterward.

### Documentation and completion gate

- Document node creation, identity reconstruction, movement, safe removal, operation reconciliation, and re-inspection requirements.
- Complete the phase when supported nodes can be created, positioned, removed, compiled, saved, restarted, and inspected entirely through MCP calls.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
