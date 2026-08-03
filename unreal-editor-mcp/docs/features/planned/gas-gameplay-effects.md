---
feature_id: gas-gameplay-effects
status: planned
depends_on:
  - gas-gameplay-effects-inspect
released_in: null
---

# `gas-gameplay-effects` — Gameplay Effect creation and updating

**Outcome:** Agents can create, configure, compile, save, and read back `UGameplayEffect`-derived data-only Blueprint assets through the `UnrealMCPGAS` companion after the family inspection contract is available.

**Depends on:**

- [`gas-gameplay-effects-inspect`](../completed/gas-gameplay-effects-inspect.md)

### Creation and update implementation

- Reuse the independently versioned optional `UnrealMCPGAS` companion, its strict companion API requirement, foundation registry, shared authenticated bridge, Game-thread dispatch, operation ledger, errors, limits, packaging, and inspection capability established by `gas-gameplay-effects-inspect`. Do not add Gameplay Ability System dependencies to `UnrealMCP` or create another listener, credential, Python package, or extension API.
- Add the `gameplay_effect` mutation capability only when the companion and prerequisite inspection capability are verified live. Accept native or Blueprint-generated parents for creation and updates only when they resolve to usable `UGameplayEffect` subclasses and pass the established class, package, mount, stale-state, and mutation-target policies.
- Reuse `blueprint_create`, `blueprint_default_edit`, `blueprint_compile`, and `blueprint_save`; use the prerequisite `blueprint_inspect` extension for stale-safe snapshots and read-back. Continue rejecting Actor components, variables, functions, macros, events, action cataloging, graph editing, and every supplied graph or member operation.
- Add exact `blueprint_default_edit` Gameplay Effect operation discriminators for adding, updating, removing, and, where semantically meaningful, reordering one supported nested record. Require the current authoritative inspection snapshot plus stable record identity for existing entries; never retarget by array index after a stale change.
- Support only the small explicit allowlist of Gameplay Effect magnitude forms, modifier operations, and public `UGameplayEffectComponent` subclasses proven by the inspection feature against Unreal Engine 5.8 public APIs. Resolve Gameplay Attributes, Gameplay Tags, Gameplay Effects, Gameplay Abilities, calculation classes, curves, and Cue tags through typed live validation; never create missing tags, accept raw property paths, or expose unrestricted struct or reflection editing.
- Validate allowed component multiplicity, ownership, outer, inheritance, cross-record references, component-specific data, and all supported cross-field rules before mutation. Invoke required live change and validation hooks, use one editor transaction per accepted mutation, verify the complete changed boundary, compile explicitly, preserve prior dirty state on rejection, and restore the exact inspected state after unexpected failure.
- Preserve unrelated inherited and local modifiers, components, policies, queries, requirements, tags, cues, class references, order, generated-class state, package contents, and referenced assets. Keep runtime Gameplay Effect Specs, live application or removal, Ability System Component mutation, Attribute Set authoring, Gameplay Cue asset authoring, arbitrary Gameplay Effect Component classes, supplied calculations, and C++ generation outside this feature.

### Verification

- Create representative instant-damage, finite-duration buff, periodic-regeneration, infinite-tag-grant, stacking and overflow, Gameplay Cue, granted-ability, and chained-effect assets using only the released typed forms. Compile, save, restart, and verify exact read-back through the prerequisite inspection contract.
- Test native and Blueprint-generated parents, inherited versus local values, supported modifier magnitude forms, attribute and tag resolution, component multiplicity, execution and calculation references, nested identity stability, ordering, and live data validation.
- Test invalid duration and period combinations, incompatible attributes and classes, nonexistent tags, cyclic or excessive effect chains, unsupported components and magnitude forms, malformed collections, duplicate identities, stale snapshots, compile failure, transaction restoration, undo and redo, timeout, replay, lost-response recovery, and response and scan limits.
- Test absent, inspection-only, mismatched, disabled, stale, and unsupported companion states without partially registering or executing mutation handlers. Prove every graph, member, and action operation still rejects without mutation.
- Prove accepted and rejected updates preserve fingerprints for all unrelated effect records, generated Blueprint data, referenced assets, package dirtiness, and base-plugin behavior. Run focused public-header probes, native Automation, packaging and version-contract tests, and full cross-process create, update, save, and restart verification on Windows against the supported Unreal Engine build. Repeat on macOS when available as non-blocking follow-up.

### Documentation and completion gate

- Document supported Gameplay Effect creation and update forms, typed reference and nested-identity requirements, cross-field validation, compilation and saving, limits, exclusions, recovery, and complete update examples. Link to the prerequisite inspection guide instead of repeating its read contract.
- Update companion capability, packaging, independent semantic-version, and companion API documentation only where mutation support changes them; retain one shared `UnrealMCPGAS` distribution.
- Complete the feature only when representative Gameplay Effects can be created, typed-updated, compiled, saved, restarted, and read back without graphs or unrelated-content changes, inspection-only or mismatched companions cannot expose or execute mutation operations, and the complete base and GAS suites pass on both native platforms.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
