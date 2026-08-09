---
feature_id: event-macro-replace
status: completed
depends_on:
  - function-replace
released_in: "0.33.0"
---

# `event-macro-replace` — Event, custom-event, and macro replacement

**Outcome:** Agents can transactionally replace one complete user-owned macro, custom-event handler, or native-event-rooted handler while preserving unrelated Blueprint content and declared boundary links.

**Depends on:**

- [`function-replace`](function-replace.md)

### Implementation

- `blueprint_block_replace` accepts exact function, macro, custom-event, and native-event shapes through one scratch-preflight, compile, semantic-fingerprint, operation-ledger, transaction, rollback, and preservation engine.
- Macro ownership is the complete graph between required tunnel nodes. Event ownership starts at one exact root, follows private execution descendants and private pure data dependencies, and cuts shared control/data nodes out of the handler.
- Event/custom-event requests must echo every inspected external crossing link and reconnect each external endpoint to an internal semantic endpoint. Direct boundary links are supported; conversions remain explicit internal plan nodes.
- Every changed node, preserved root/tunnel, and automatic conversion retains an explicit bounded position. Automatic layout remains deferred to `node-layout`.

### Verification

- Python contracts cover all exact request variants, bounds, and legacy complete-function compatibility.
- Native Automation covers macro, custom-event, and native-event replacement, shared-node boundaries, external links, scratch/live parity, compile failure, stale/invalid boundaries, rollback, Undo/Redo, and preservation.
- The Windows production bridge workflow replaces all three new logic-unit families, reconciles retained mutations, compiles/saves, restarts the editor, and verifies identities, fingerprints, links, and unrelated content.
- Preferred macOS native verification remains in the roadmap platform backlog; Linux is outside the supported native scope.

### Documentation and completion gate

Ownership, exact request shapes, limits, recovery, scratch duplication identity rebinding, and explicit positioning are documented in the architecture, type, and user guides. Windows implementation and release validation are complete for 0.33.0.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
