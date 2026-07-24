# Phase 31 — Spline component inspection and editing

**Outcome:** Agents can inspect and transactionally edit bounded spline point sets on exact level actor components, then save and verify them across reload.

### Implementation

- Extend `level_inspect` with a spline section that reports component identity, closed-loop state, coordinate space, point order, point type, position, rotation, scale, arrive tangent, and leave tangent.
- Extend `level_actor_edit` with exact spline operations for closed-loop state, add, update, reorder, and remove. Require actor and component identities plus the latest map snapshot.
- Bound spline components, point counts, batch operations, coordinates, rotations, scales, tangents, and response size. Reject non-finite values and invalid point indices or identities.
- Prevalidate the complete spline batch, modify once inside the owning actor transaction, update the spline once after the batch, and verify exact point order and values before commit.
- Preserve supported spline metadata during point insertion, removal, and reorder. Reject metadata-bearing spline subclasses when their metadata cannot be preserved through public Unreal 5.8 APIs; never silently discard custom metadata.
- Integrate spline changes with World Partition targeted loading, external-package tracking, `level_save`, operation replay, stale snapshots, rollback, undo/redo, and reload verification.

### Verification

- Test open and closed splines, local and world coordinates, mixed point types, rotations, scales, independent tangents, insertion, removal, reorder, invalid indices, non-finite values, limits, stale snapshots, replay, rollback, and undo/redo.
- Test native and Blueprint-provided spline components, construction-script interactions, supported metadata preservation, unsupported metadata rejection, external actors, save/reload, and exact read-back.
- Run the level-authoring spline acceptance with at least four mixed-type points on macOS and Windows.

### Documentation and completion gate

- Document spline identities, coordinate and tangent semantics, point operations, metadata policy, transactions, World Partition saving, verification, and limits.
- Add a traversal-spline example with four mixed-type points, explicit tangents, save, reload, and exact read-back.
- Complete the phase only when a mixed-point traversal spline survives save and reload with exact supported values and no duplicate points on operation replay.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
