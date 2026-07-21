# Phase 4 — Reliable mutations, Actor components, and defaults

**Outcome:** Agents can reliably reconcile mutation outcomes, build an Actor component hierarchy, and edit supported component and Blueprint class defaults without duplicate execution or ambiguous timeouts.

### Implementation

- Add the process-scoped mutation ledger, caller-generated operation IDs, request digests, bridge-instance identity, retained-result replay, `operation_status`, bounded eviction, and explicit cancellation/unknown-outcome semantics described in the baseline. Apply the contract to every mutating tool.
- Add `blueprint_component_edit` operations for one add, remove, rename, reparent, root change, or component-default property change.
- Use the public Subobject Data Subsystem and Simple Construction Script APIs only after native behavioral probes prove add, delete, rename, reparent, root, notification, identity, and undo behavior in Unreal 5.8.
- Distinguish inherited, native, instanced, and locally owned components. Permit mutation only where ownership and the live Blueprint component capability allow it.
- Accept only suitable `UActorComponent`-derived classes. Reject abstract, deprecated, editor-only, transient, incompatible, non-spawnable, or otherwise invalid component classes before mutation.
- Enforce unique names, legal attachments, scene/non-scene rules, root invariants, ownership, stable component identity, and current-snapshot preconditions.
- Add targeted inspection of editable component and Blueprint class-default properties. Avoid dumping every reflected property.
- Add `blueprint_default_edit` for one supported editable property on the Blueprint-generated class default object. Use the same codec and property policy as component defaults.
- Begin the shared property codec with booleans, finite numbers, strings/names/text, enums and flags, common math/color/transform structs, native and Blueprint class references, and visible packageable asset references. Reject delegates, raw pointers, arbitrary object graphs, unsupported containers, transient/editor-only objects, and properties without safe editable semantics.
- Return the retained operation outcome, new snapshot, concise changed record, dirty state, and any identities reconstructed by the edit. Compilation and saving remain explicit.

### Verification

- Test lost responses, same-ID replay, conflicting ID reuse, ledger bounds and expiry, queued cancellation, commit-before-disconnect, bridge restart, unknown outcomes, stale snapshots, and inspect-before-retry recovery.
- Test scene and non-scene components, root creation and replacement, attachment cycles, duplicate names, invalid component classes, inherited/native component rejection, reparenting, rename, and removal.
- Test every supported property form on component templates and the Blueprint class default object, including native classes and visible engine/project/plugin asset references, incompatible references, unsupported values, and exact read/write round trips.
- Test undo/redo, compile, save, reload, and identity behavior. Prove through structural and dirty-state comparison that rejection and unexpected failure leave no partial mutation or misleading transaction record.

### Documentation and completion gate

- Document operation reconciliation, component identities and ownership, root and attachment rules, supported property forms, reference policy, explicit compile/save workflow, and Undo recovery.
- This phase completed after MCP calls created and persisted a multi-component Actor Blueprint, edited an Actor class default, and safely reconciled a deliberately lost mutation response.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
