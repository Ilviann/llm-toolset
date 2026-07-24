# Phase 30 — Transactional level actor editing and verified saving

**Outcome:** Agents can create or mutate a bounded actor batch against an exact map snapshot and explicitly save and verify the affected map and external-actor packages.

### Implementation

- Add ledger-backed `level_actor_edit` with exact discriminated batch operations for spawn, transform, label, tags, folder, data layers, attachment, exposed instance properties, supported component instance properties, move, and delete.
- Require `operation_id`, current map identity, `expected_snapshot`, exact identities for existing actors/components, and bounded operation counts. Resolve native and Blueprint spawn classes through mounted class paths and reject abstract, deprecated, editor-only, incompatible, transient, or unsafe classes.
- Prevalidate the complete batch before mutation. Use one editor transaction where supported, a bounded explicit rollback journal for created/deleted/changed actors and dirty states, and postcondition read-back before reporting commit.
- Preserve Actor GUIDs for existing actors. Let Unreal create valid GUIDs and external packages for new World Partition actors, then return the exact created identities and affected package set.
- Load only required actors or bounded regions. Reject locked data layers, unavailable cells, unresolved attachments, attachment cycles, incompatible properties, stale snapshots, unsafe editor state, and conflicting operations before mutation.
- Add ledger-backed `level_save` for the current map and an explicit bounded set of affected external-actor packages. Preflight writability and package state, save non-interactively, return one result per package, and re-inspect or reload on request to verify expected identities and values.
- Do not claim cross-package filesystem atomicity. Return `partial_failure` with exact saved and failed package identities if Unreal persists only part of a package set; preserve enough state and diagnostics for safe inspection and recovery.
- Refuse level mutation and saving during PIE, simulation, another save, garbage collection, undo/redo, map loading, or another conflicting operation.

### Verification

- Test native and Blueprint spawning, every metadata/property operation, attachments and cycles, data layers, component properties, movement, deletion, batch bounds, full prevalidation, rollback, undo/redo, stale snapshots, replay, conflicting operation IDs, and lost-response reconciliation.
- Test World Partition externalization, targeted cell loading, GUID preservation, dirty packages, unwritable and partially failing package sets, save result attribution, reload verification, and retry after partial failure.
- Run the level-authoring acceptance on macOS and Windows with three Blueprint actors and one native actor, exact read-back, restart persistence, idempotent replay, and stale-write rejection.

### Documentation and completion gate

- Document the edit operation matrix, property/class policy, transaction and rollback semantics, World Partition loading, affected-package results, verified save/reload, partial failure, recovery, and limits.
- Add an actor-placement example that edits a bounded mixed native/Blueprint batch, saves the returned package set, reloads, and verifies exact identities and values.
- Complete the phase only when bounded actor batches are prevalidated, idempotent, stale-safe, rollback-verified in memory, and persisted with honest per-package results.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
