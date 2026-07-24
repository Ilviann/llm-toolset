# Phase 29 — World Partition actor and instance inspection

**Outcome:** Agents can inspect bounded actor pages and selected actor/component instance state in the current map without loading the entire World Partition world.

### Implementation

- Extend `level_inspect` with exact actor-list, actor, component, and property sections bound to the current map identity and snapshot.
- Use map-qualified Actor GUIDs as stable level-actor identities. Report label, class, transform, tags, folder, data-layer membership, attachment parent, loaded state, World Partition descriptor state, spatial bounds when available, external-actor package identity, and package dirtiness.
- Enumerate unloaded World Partition actors from actor descriptors. Use exact identity or bounded-region loading only when live actor, component, or reflected instance data is requested; report load and data-layer failures rather than treating unavailable actors as absent.
- Support exact filters for actor identity, label, class, tag, folder, data layer, loaded state, and bounded region. Bound descriptor scans, actors returned, targeted loads, components, property names, nested values, response bytes, cursors, and retained snapshots.
- Extract or generalize the existing reflected-value codec for safe UObject instance reads. Expose only requested supported editable/visible properties and reject transient, editor-only, delegate, instanced-object-graph, unsafe reference, and unsupported value forms.
- Give components stable actor-scoped identities and distinguish native default, Blueprint-created, construction-script, and instance components where Unreal exposes that distinction reliably.
- Preserve editor selection, loaded-region state, dirty state, transactions, and actor/package state during read-only inspection.

### Verification

- Test loaded and unloaded actors, exact GUID and region lookup, every filter, attachments, folders, tags, data layers, external packages, native and Blueprint classes, component identities, supported values, unsupported properties, cursor staleness, response bounds, and load failures.
- Prove broad inspection does not load the entire World Partition world and targeted inspection restores temporary loading state when safe.
- Test selection, dirty-state, transaction, loaded-region, and snapshot preservation before and after inspection on macOS and Windows.

### Documentation and completion gate

- Document actor/component identities, descriptor versus live-instance fields, targeted loading, filters, property policy, pagination, snapshots, and unavailable-state errors.
- Add a World Partition example that filters a bounded region, continues one page, and inspects selected actor/component properties by exact identity.
- Complete the phase only when bounded inspection can locate and read the actors needed for level authoring without unbounded world loading or editor mutation.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
