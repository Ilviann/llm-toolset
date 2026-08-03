---
feature_id: gas-ability-blueprints
status: planned
depends_on:
  - phase-13
  - gas-ability-blueprints-inspect
released_in: null
---

# `gas-ability-blueprints` — Gameplay Ability Blueprint creation and updating

**Outcome:** Agents can create, configure, graph-edit, compile, save, and read back `UGameplayAbility`-derived Blueprint assets through the optional `UnrealMCPGAS` companion after the family inspection contract is available.

**Depends on:**

- [`phase-13`](../completed/phase-13.md)
- [`gas-ability-blueprints-inspect`](../completed/gas-ability-blueprints-inspect.md)

### Creation and update implementation

- Reuse the independently versioned optional `UnrealMCPGAS` companion, its strict companion API requirement, foundation registry, shared authenticated bridge, Game-thread dispatch, operation ledger, errors, limits, packaging, and inspection capability established by `gas-ability-blueprints-inspect`. Do not add Gameplay Ability System dependencies to `UnrealMCP` or create another listener, credential, Python package, or extension API.
- Add the `gameplay_ability` mutation capability only when the companion and prerequisite inspection capability are verified live. Accept native or Blueprint-generated parents for creation and updates only when they resolve to usable `UGameplayAbility` subclasses and pass the established stale-class, compilation, package, mount, and mutation-target policies.
- Reuse `blueprint_create`, `blueprint_default_edit`, `blueprint_member_edit`, `blueprint_action_catalog`, `blueprint_graph_edit`, `blueprint_compile`, and `blueprint_save`; use the prerequisite `blueprint_inspect` extension for stale-safe snapshots and read-back. Do not publish a separate model-facing GAS tool or a general UObject or reflection editor.
- Mutate only a small explicit allowlist of Gameplay Ability configuration proven against Unreal Engine 5.7 public APIs, including supported activation, instancing, and network policies; Gameplay Tag and trigger containers; and compatible cost and cooldown Gameplay Effect class references. Require the authoritative inspection snapshot and stable nested identity for every update to existing data.
- Extend the action catalog with only context-valid Gameplay Ability events, functions, and Ability Task nodes returned by Unreal's live Blueprint action database. Preserve opaque action identities and existing graph, node, and pin limits; do not synthesize arbitrary GAS calls or bypass the live K2 schema.
- Preserve unrelated graphs, members, defaults, inherited settings, tags, triggers, class references, comments, positions, bookmarks, generated-class state, package contents, and prior dirty state. Continue using explicit compilation and saving, bounded diagnostics, one transaction per accepted mutation, postcondition verification, retained operation reconciliation, and exact restoration on unexpected failure.
- Reuse released Gameplay Effect asset inspection from [`gas-gameplay-effects-inspect`](../completed/gas-gameplay-effects-inspect.md) and defer authoring to [`gas-gameplay-effects`](gas-gameplay-effects.md). Keep Attribute Set authoring, Gameplay Cue asset authoring, Ability System Component setup on Actors, input binding, project GAS configuration, runtime ability granting or activation, PIE execution, and arbitrary custom Ability Task or C++ generation outside this feature.

### Verification

- Create representative abilities from native and Blueprint-generated parents. Update every supported policy, tag and trigger form, and cost and cooldown reference; add representative context-valid ability events, ordinary graph nodes, and Ability Task nodes; compile, save, restart, and verify exact read-back through the prerequisite inspection contract.
- Test invalid parents and references, unsupported policies and GAS asset families, malformed or excessive tag and trigger collections, stale snapshots and identities, compile failure, transaction restoration, undo and redo, timeout, replay, lost-response recovery, and unchanged-content fingerprints.
- Test absent, inspection-only, mismatched, disabled, stale, and unsupported companion states without partially registering or executing mutation handlers. Verify mutation capability registration and removal across editor restart and plugin enablement changes.
- Prove accepted and rejected changes preserve all unrelated Blueprint content, referenced assets, package state, and base-plugin behavior. Run the complete base and GAS companion suites natively on Windows against the supported Unreal Engine build. Repeat on macOS when available as non-blocking follow-up.

### Documentation and completion gate

- Document the additional mutation capability, supported Gameplay Ability creation and update workflows, snapshot and nested-identity requirements, compilation and saving, limits, exclusions, recovery, and focused creation and update examples. Link to the prerequisite inspection guide instead of repeating its read contract.
- Update companion installation, packaging, independent semantic-version, and companion API documentation only where mutation support changes them; retain one shared `UnrealMCPGAS` distribution and never download or enable dependencies at runtime.
- Complete the feature only when representative Gameplay Ability Blueprints can be created, configured, graph-edited, compiled, saved, restarted, and read back without altering unrelated content, inspection-only or mismatched companions cannot expose or execute mutation operations, and the complete base and GAS suites pass on both native platforms.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
