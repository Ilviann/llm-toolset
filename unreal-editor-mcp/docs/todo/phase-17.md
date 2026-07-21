# Phase 17 — Complete function replacement

**Outcome:** Agents can replace one complete user-owned function as a single prevalidated operation while preserving unrelated Blueprint content.

### Implementation

- Add `blueprint_block_replace` using the existing member, action-catalog, type/value, graph-edit, operation-ledger, and diagnostic primitives rather than a second mutation engine.
- Limit the first contract to one complete user-owned function. Treat the function entry, result, parameters, and locals as the complete declared boundary; keep arbitrary graph regions, events, custom events, and macros unsupported.
- Define required entry and result identities, owned nodes, local variables, replacement operations, action signatures, limits, expected fingerprints, explicit node positions, and current-snapshot preconditions.
- Preflight without supplied code or free-form Blueprint text. Use an isolated non-transient scratch Blueprint/package or a semantic preflight proven behaviorally equivalent to live spawning; do not assume transient-graph node spawning matches a live graph.
- Compile the isolated candidate, compare planned postconditions, and remove all scratch objects and registrations before touching the live Blueprint.
- Apply the validated live replacement as one reconciled editor transaction. Preserve unrelated graphs, nodes, variables, metadata, links, positions, bookmarks, comments, and prior dirty state; explicitly restore on any unexpected result.
- Require explicit positions for changed nodes. Automatic layout remains unsupported until Phase 19.

### Verification

- Replace representative pure and impure user-owned functions with parameters, results, locals, internal branches, cycles, latent restrictions, and supported conversions.
- Test invalid boundaries, stale snapshots, expired actions, compile failure, timeout, lost response, rollback, undo/redo, save/reload, and unchanged-content fingerprints.
- Prove scratch preflight/live parity for every supported node family. Prove failed preflight creates no live transaction and unexpected live failure restores exact inspected structure and prior dirty state.
- Run the complete function-replacement and preservation suites natively on macOS and Windows.

### Documentation and completion gate

- Document function ownership, declared boundaries, scratch preflight, explicit positions, preservation guarantees, limits, operation recovery, and when atomic actions remain preferable.
- Complete the phase only when unrelated-content fingerprints remain stable across successful, rejected, timed-out, and replayed function replacements on both native platforms.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
