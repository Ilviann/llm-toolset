---
feature_id: level-edit
status: completed
depends_on:
  - level-inspect
released_in: "0.26.0"
---

# `level-edit` — Transactional level actor editing and verified saving

**Outcome:** Agents can create or mutate a bounded actor batch against an exact map snapshot and explicitly save and verify the affected map and external actor/object packages.

**Depends on:**

- [`level-inspect`](level-inspect.md)

**Implementation status:** Completed in 0.26.0 and verified natively on Windows. macOS verification remains in the native platform backlog.

### Implementation

- `level_actor_edit` is ledger-backed and accepts at most 32 exact discriminated operations: native or Blueprint spawn, transform, label, tags, folder, data-layer set replacement, attach, detach, exposed actor/component property assignment, non-World-Partition loaded-level move, and delete.
- Every batch carries an operation ID, exact current map identity, and current snapshot. Existing actor/component identities are exact; the entire bounded batch, class/property policy, attachment graph, data layers, levels, and conflicts are prevalidated before one editor transaction begins.
- Existing Actor GUIDs are preserved. Unreal assigns new GUIDs and World Partition packages for spawned actors, and results return exact actor identities, operation read-back, the advanced snapshot, and the complete loaded dirty map-owned package set.
- Required World Partition actors are loaded through scoped references only. Locked or missing data layers, unavailable actors, unsafe classes/properties/editor states, attachment cycles, stale snapshots, delete/write conflicts, and unsupported moves reject without mutation.
- Runtime failures invoke Undo plus a bounded actor/package journal and are reported as committed only when rollback is verified. Successful edits remain Undo/Redo-capable and do not save implicitly.
- `level_save` accepts only the exact current map and returned bounded package set. It preflights package ownership and writability, saves non-interactively, distinguishes persisted packages from verified deleted empty external packages, and returns per-package evidence.
- Save verification can inspect live state or reload the root map, then checks requested actor identities, labels, transforms, tags, folders, actor properties, component identities, and component properties. Cross-package filesystem atomicity is never claimed; incomplete persistence or read-back returns a retained partial outcome with exact saved/failed packages.

### Verification

- Python contracts cover exact schemas, bounds, command publication, ledger routing, capabilities, class/property policy, transaction/rollback code paths, and save evidence.
- `UnrealMCP.LevelEdit.TransactionalActorBatchAndPackageSave` covers mixed native/Blueprint World Partition spawning, metadata and component changes, attachment, deletion, GUID-qualified identities, stale rejection, external actor/folder packages, verified deletion, save, reload, and exact read-back on Windows.
- The production headless scenario covers mixed actor placement, replay, stale-write rejection, explicit returned-package saving, reload, restart-stable identities/values, and cleanup through reference-aware map deletion.

### Documentation and completion gate

- The operation matrix, class/property policy, transaction behavior, World Partition scope, package results, verification, recovery, and limits are documented in the [level actor editing contracts](../../types/level-actor-editing-service/contracts.md) and [levels and assets guide](../../user/levels-and-assets.md#level-actor-editing-and-saving).
- [`examples/level-edit-workflow.json`](../../../examples/level-edit-workflow.json) provides a complete mixed native/Blueprint placement, edit, save, reload-verification, and reconciliation sequence.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
