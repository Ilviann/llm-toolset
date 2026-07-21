# Phase 5 — Blueprint member variables

**Outcome:** Agents can inspect and safely define the typed member variables needed by Actor Blueprint code.

### Implementation

- Extend `blueprint_inspect` with bounded member-variable type, default, metadata, ownership, replication, and reference-summary records. Return stable identities and exact relationships needed for mutation preflight.
- Add `blueprint_member_edit` operations for one add, rename, supported update, or safe removal of a member variable.
- Reuse the canonical K2 type/value codec. Define explicit scalar, container, object/class reference, and default-value forms rather than accepting serialized engine structures.
- Validate names, inherited and cross-kind collisions, type compatibility, defaults, replication, instance editability, visibility, and categories through live Blueprint capabilities.
- Expose RepNotify relationships for inspection, but defer mutations that create or alter notification functions to Phase 6.
- Use `reject_if_referenced` as the only removal or type-change policy. Do not cascade-delete nodes, silently orphan references, or attempt automatic graph repair.
- Return the operation result, concise member identity, reference summary, reconstructed identities, and new Blueprint snapshot.

### Verification

- Test every supported variable type and metadata flag, invalid names, inherited and cross-kind collisions, defaults, replication metadata, and unsupported Blueprint capabilities.
- Test member references, safe removal, rejected referenced removal, undo/redo, compilation, saving, restart, and read-back.
- Prove that inspection exposes enough information to plan every accepted variable mutation and that rejected mutations preserve structure, dirty state, compile state, and transaction history.

### Documentation and completion gate

- Document the shared variable-type vocabulary, metadata and reference rules, RepNotify deferral, and inspect-before-edit examples.
- Complete the phase only when variables survive compile, save, restart, and exact bounded inspection.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
