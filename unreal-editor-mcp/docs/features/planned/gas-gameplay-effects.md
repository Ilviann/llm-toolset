---
feature_id: gas-gameplay-effects
status: planned
depends_on:
  - gas-ability-blueprints
released_in: null
---

# `gas-gameplay-effects` — Gameplay Effect creation and editing

**Outcome:** Agents can create, inspect, configure, compile, save, and read back `UGameplayEffect`-derived data-only Blueprint assets through the lockstep-versioned `UnrealMCPGAS` companion while preserving unrelated effect configuration.

**Depends on:**

- [`gas-ability-blueprints`](gas-ability-blueprints.md)

### Implementation

- Reuse the optional `UnrealMCPGAS` companion, its exact base-plugin version requirement, narrow extension interface, shared authenticated bridge, Game-thread dispatch, operation ledger, errors, limits, packaging, and capability registration. Do not add Gameplay Ability System dependencies to `UnrealMCP` or create another listener, credential, Python package, or release version.
- Add an explicit `gameplay_effect` data-only Blueprint family while the verified companion capability is live. Accept native or Blueprint-generated parents only when they resolve to usable `UGameplayEffect` subclasses and pass the established class, package, mount, stale-state, and mutation-target policies.
- Reuse `blueprint_create`, `blueprint_inspect`, `blueprint_default_edit`, `blueprint_compile`, and `blueprint_save`. Publish exact family capabilities that reject components in the Actor sense, variables, functions, macros, events, action cataloging, graph editing, and every supplied graph/member operation; Gameplay Effects are configuration assets and must not acquire Blueprint logic graphs.
- Extend inspection with bounded typed sections for the supported duration and period policies, modifiers and magnitudes, executions, stacking and overflow behavior, Gameplay Cues, granted abilities, tag requirements, additional-effect references, and instanced Gameplay Effect Components. Report inherited versus locally declared data, compatible class/attribute/tag references, stable nested identities, truncation, and an authoritative snapshot.
- Extend `blueprint_default_edit` with exact Gameplay Effect operation discriminators for adding, updating, removing, and where semantically meaningful reordering one supported nested record. Require the current asset snapshot plus stable record identity for existing entries; never retarget by array index after a stale change.
- Support a small explicit allowlist of Gameplay Effect magnitude forms and modifier operations proven through Unreal Engine 5.7 public APIs. Resolve Gameplay Attributes, Gameplay Tags, Gameplay Effects, Gameplay Abilities, calculation classes, curves, and Cue tags through typed live validation; never create missing tags, accept raw property paths, or expose unrestricted struct/reflection editing.
- Support only explicitly allowlisted public `UGameplayEffectComponent` subclasses and typed fields. Validate allowed multiplicity, ownership, outer, inheritance, cross-record references, and component-specific data rules before mutation; invoke required live change/validation hooks and reject unsupported native or project-defined component layouts rather than reflecting them generically.
- Prevalidate cross-field rules for instant, duration, infinite, and periodic effects; modifier/execution applicability; stacking/overflow; granted abilities; tags; cues; and chained effects. Use one editor transaction per accepted mutation, verify the complete changed boundary, compile explicitly, preserve prior dirty state on rejection, and restore exact inspected state after unexpected failure.
- Preserve unrelated inherited and local modifiers, components, policies, queries, requirements, tags, cues, class references, order, generated-class state, and package contents. Keep runtime Gameplay Effect Specs, live application/removal, Ability System Component mutation, Attribute Set authoring, Gameplay Cue asset authoring, arbitrary Gameplay Effect Component classes, supplied calculations, and C++ generation outside this feature.

### Verification

- Create representative instant damage, finite-duration buff, periodic regeneration, infinite tag grant, stacking/overflow, Gameplay Cue, granted-ability, and chained-effect assets using only the released typed forms. Compile, save, restart, and verify exact inspection read-back and references from Gameplay Ability cost/cooldown settings.
- Test native and Blueprint-generated parents, inherited-versus-local values, supported modifier magnitude forms, attribute and tag resolution, component multiplicity, execution/calculation references, nested identity stability, ordering, and live data validation.
- Test invalid duration/period combinations, incompatible attributes and classes, nonexistent tags, cyclic or excessive effect chains, unsupported components and magnitude forms, malformed collections, duplicate identities, stale snapshots, compile failure, transaction restoration, undo/redo, timeout, replay, lost-response recovery, and response/scan limits.
- Prove every graph/member/action operation rejects without mutation. Prove accepted and rejected edits preserve fingerprints for all unrelated effect records, generated Blueprint data, referenced assets, package dirtiness, and base-plugin behavior.
- Run focused public-header probes, normal/forced-unity/non-unity builds, native Automation, packaging/version-contract tests, and full cross-process create/edit/save/restart verification on macOS and Windows against the supported Unreal Engine build.

### Documentation and completion gate

- Document data-only Gameplay Effect semantics, supported duration/modifier/component/magnitude forms, typed reference and nested-identity rules, inheritance, limits, exclusions, recovery, and complete effect examples connected to Gameplay Ability cost/cooldown fields.
- Update companion installation, capability, packaging, and lockstep release documentation only where the Gameplay Effect surface changes them; retain one shared `UnrealMCPGAS` distribution.
- Complete the feature only when representative Gameplay Effects can be created, typed-edited, compiled, saved, restarted, and read back without graphs or unrelated-content changes, mismatched companions cannot expose or execute the operations, and the complete base and GAS suites pass on both native platforms.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
