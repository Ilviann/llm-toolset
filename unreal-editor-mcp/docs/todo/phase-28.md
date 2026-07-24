# Phase 28 — Level discovery, safe opening, and snapshot foundations

**Outcome:** Agents can discover mounted maps, safely open one exact map, and obtain a stable current-map snapshot suitable for bounded level inspection and stale-write protection.

### Implementation

- Resolve the open Windows exact-snapshot instability in [`../issues/issue-1.md`](../issues/issue-1.md) before reusing snapshot infrastructure for level persistence. Preserve meaningful structural differences rather than weakening equality checks.
- Add `level_inspect` discovery and current-map-summary modes. Discover mounted World assets through the Asset Registry with bounded scans, pages, cursors, and concise map records.
- Add a separate ledger-backed `level_open` tool. Accept one mounted map asset path and caller-generated `operation_id`; never accept a filesystem path.
- Reject map switching while the current map is dirty, PIE or simulation is active, saving or garbage collection is active, another conflicting operation is retained, or editor state cannot be proven safe. Do not implicitly save, discard, or prompt.
- Define exact map identity, map revision, query-bound `snapshot_id`, cursor, dirty-state, World Partition, external-actor, and current-map records. Invalidate revisions on map load, actor/package changes, undo/redo, and other observed editor mutations.
- Keep discovery read-only. Make repeated identical `level_open` operations idempotent and return the new exact current-map summary and snapshot.
- Add compiled public-API probes for map discovery, editor map loading, world identity, package dirtiness, World Partition access, and relevant editor delegates on Unreal Engine 5.8.

### Verification

- Test bounded map discovery, pagination, cursor expiry, invalid and non-map assets, unsupported mounts, dirty-map refusal, PIE/simulation/save/GC refusal, conflicting work, timeout, replay, and operation-ID conflict.
- Test non-World-Partition and World Partition maps, repeated opening of the current map, map transitions, undo/redo invalidation, external editor mutations, and exact save/restart snapshot stability.
- Run the focused native cases and cross-process persistence workflow on macOS and Windows; preserve Linux source portability and compiled compatibility branches.

### Documentation and completion gate

- Document `level_inspect` discovery/current-map modes, `level_open`, map identities, revisions, snapshots, cursors, refusal states, idempotency, and limits.
- Add a bounded example that discovers maps, safely opens one clean target, and re-inspects the returned snapshot.
- Complete the phase only when an agent can discover and safely open an exact map, obtain a restart-stable snapshot, and cannot cause implicit save or data loss.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
