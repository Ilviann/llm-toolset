# Phase 18 — Deterministic changed-node layout

**Outcome:** Block replacement can lay out changed nodes predictably without moving or otherwise altering untouched Blueprint content.

### Implementation

- Add a deterministic bounded layout option to `blueprint_block_replace`; keep explicit caller-supplied positions supported.
- Lay out changed nodes only. Preserve untouched positions and handle execution flow, data dependencies, cycles, comments, graph bounds, macro tunnels, and inserted conversion nodes predictably.
- Include layout inputs and policy in preflight, operation identity, limits, expected fingerprints, transaction application, and postcondition verification.
- Reject layouts that exceed graph, node, iteration, coordinate, transaction-work, or Game-thread limits before touching the live Blueprint.

### Verification

- Test deterministic placement across functions, event handlers, custom events, and macros with branches, joins, cycles, comments, external links, and inserted conversion nodes.
- Prove repeated equivalent plans produce identical changed-node positions and preserve all untouched positions and unrelated-content fingerprints.
- Test bounds, timeout, rollback, undo/redo, save/reload, replay, and lost-response recovery natively on macOS and Windows.

### Documentation and completion gate

- Document the layout policy, determinism, bounds, preservation guarantees, explicit-position alternative, and recovery behavior.
- Complete the phase only when layout is deterministic and unrelated-content fingerprints remain stable across success, rejection, failure, timeout, and replay on both native platforms.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
