---
feature_id: gas-gameplay-effects-inspect
status: planned
depends_on:
  - gas-ability-blueprints-inspect
released_in: null
---

# `gas-gameplay-effects-inspect` — Gameplay Effect inspection

**Outcome:** Agents can discover and inspect existing `UGameplayEffect`-derived data-only Blueprint assets through the lockstep-versioned `UnrealMCPGAS` companion without changing the asset or its package.

**Depends on:**

- [`gas-ability-blueprints-inspect`](gas-ability-blueprints-inspect.md)

### Read and inspection implementation

- Reuse the optional `UnrealMCPGAS` companion, exact base-plugin version requirement, narrow extension interface, shared authenticated bridge, Game-thread dispatch, errors, limits, packaging, and capability registration established by `gas-ability-blueprints-inspect`. Do not add Gameplay Ability System dependencies to `UnrealMCP` or create another listener, credential, Python package, or release version.
- Add an explicit inspection-only `gameplay_effect` data-only Blueprint family while the verified companion capability is live. Accept native or Blueprint-generated parents only when they resolve to usable `UGameplayEffect` subclasses and pass the established class, package, mount, stale-class, and asset-access policies.
- Reuse `blueprint_inspect` and publish exact family capabilities that reject Actor components, variables, functions, macros, events, action cataloging, graph editing, and every supplied graph or member operation. Do not create, compile, save, or default-edit assets in this feature; Gameplay Effects are configuration assets and must not acquire Blueprint logic graphs.
- Add bounded typed inspection sections for supported duration and period policies, modifiers and magnitudes, executions, stacking and overflow behavior, Gameplay Cues, granted abilities, tag requirements, additional-effect references, and instanced Gameplay Effect Components. Report inherited versus locally declared data, compatible class, attribute, tag, curve, and asset references, stable nested identities, truncation, and one authoritative asset snapshot.
- Inspect only the explicit magnitude forms, modifier operations, and public `UGameplayEffectComponent` subclasses proven through Unreal Engine 5.8 public APIs. Return typed unsupported or unresolved records rather than accepting raw property paths, reflecting project-defined layouts generically, creating missing tags, or silently omitting unknown data.
- Validate and report cross-field relationships for instant, duration, infinite, and periodic effects; modifier and execution applicability; stacking and overflow; granted abilities; tags; cues; and chained effects without invoking change hooks or normalizing the asset.
- Keep runtime Gameplay Effect Specs, live application or removal, Ability System Component mutation, Attribute Set authoring, Gameplay Cue asset authoring, supplied calculations, arbitrary component authoring, and C++ generation outside this feature.

### Verification

- Inspect representative existing instant-damage, finite-duration buff, periodic-regeneration, infinite-tag-grant, stacking and overflow, Gameplay Cue, granted-ability, and chained-effect assets. Restart the editor and verify exact bounded read-back and cost or cooldown references from existing Gameplay Ability assets.
- Cover native and Blueprint-generated parents, inherited versus local values, every supported modifier magnitude form, attribute and tag resolution, component multiplicity, execution and calculation references, stable nested identities, ordering, and unsupported or unresolved records.
- Test invalid duration and period combinations, incompatible attributes and classes, nonexistent tags, cyclic or excessive effect chains, unsupported components and magnitude forms, malformed collections, duplicate identities, stale pages or snapshots, and response and scan limits.
- Prove every create, default-edit, graph, member, action, compile, and save operation rejects without mutation. Fingerprint all effect records, generated Blueprint data, referenced assets, package dirtiness, and Undo history before and after each successful and rejected inspection.
- Run focused public-header probes, normal, forced-unity, and non-unity builds, native Automation, packaging and version-contract tests, and full cross-process inspection verification on macOS and Windows against the supported Unreal Engine build.

### Documentation and completion gate

- Document data-only Gameplay Effect semantics, supported inspection sections and forms, typed reference and nested-identity rules, inheritance, pagination, limits, exclusions, unresolved data, and complete read-only examples connected to existing Gameplay Ability cost and cooldown fields.
- Update companion capability documentation only where the Gameplay Effect inspection surface changes it; retain one shared `UnrealMCPGAS` distribution and distinguish inspection support from mutation support.
- Complete the feature only when representative existing Gameplay Effects can be inspected after restart without graphs or any asset or package mutation, unsupported data is explicit, mismatched companions cannot expose or execute the family, and the complete base and GAS inspection suites pass on both native platforms.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
