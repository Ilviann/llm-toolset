# Phase 11 — Pin defaults and direct connections

**Outcome:** Agents can set supported pin defaults and create or remove schema-valid direct connections without automatic conversion.

### Implementation

- Add `blueprint_graph_edit` operations for `set_pin_default`, `connect_pins`, and `disconnect_pins`.
- Use the live K2 graph schema for exact pin compatibility, connection responses, default parsing, link replacement, and link breaking. Never write link arrays or unvalidated default strings directly.
- Keep automatic conversion disabled and reject connections that require an inserted conversion node.
- Require operation ID, current Blueprint snapshot, graph identity, node identity, and pin identity preconditions as applicable.
- Prevalidate the complete pin operation before opening one transaction. Verify links, defaults, ownership, reconstruction, dirty state, and structure limits afterward; restore explicitly on unexpected failure.
- Bound links per pin, default size, result size, transaction work, diagnostics, retained operation state, and Game-thread duration.

### Verification

- Test execution and data pins, compatible and incompatible types, duplicate and replacement links, disconnect behavior, defaults, and object/class/asset references.
- Test protected pins, stale and reconstructed identities, conversion-required rejection, undo/redo, compile, save, restart, reload, and lost-response reconciliation.
- Capture the graph before every rejection and injected failure and prove equality of structure, dirty state, compile state, and transaction history afterward.

### Documentation and completion gate

- Document pin identities, supported defaults, direct-connection policy, link replacement, operation reconciliation, and re-inspection after reconstruction.
- Complete the phase when MCP calls alone can build and persist a small behavior using only directly compatible node and pin types.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
