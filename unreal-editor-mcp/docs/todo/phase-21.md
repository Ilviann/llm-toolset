# Phase 21 — Event, custom-event, and macro replacement

**Outcome:** Agents can replace one complete supported event implementation, custom-event handler, or user-owned macro while preserving unrelated Blueprint content.

### Implementation

- Extend `blueprint_block_replace` to one user-owned macro, one custom-event handler, or one event-rooted implementation with bounded declared external links.
- Define ownership and boundary rules for event roots, custom events, macro tunnels, locals, latent nodes, and allowed external data or control links.
- Reuse the Phase 20 scratch preflight, compilation, fingerprint, operation reconciliation, transaction, rollback, and preservation engine without introducing family-specific mutation paths.
- Preserve unrelated graphs, nodes, variables, metadata, links, positions, bookmarks, comments, and prior dirty state.
- Continue requiring explicit positions for changed nodes. Automatic layout remains unsupported until Phase 22.

### Verification

- Replace representative event implementations, macros, and custom-event handlers with internal and declared external links.
- Test invalid boundaries, cycles, latent nodes, locals, macro tunnels, stale snapshots, expired actions, compile failure, timeout, lost response, rollback, undo/redo, save/reload, and unchanged-content fingerprints.
- Prove scratch preflight/live parity and exact restoration for every added logic-unit family.
- Run the complete replacement and preservation suites natively on macOS and Windows.

### Documentation and completion gate

- Document logic-unit ownership, boundary links, macro tunnels, event-root rules, explicit positions, preservation guarantees, limits, and recovery.
- Complete the phase only when unrelated-content fingerprints remain stable across all supported logic-unit families on both native platforms.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
