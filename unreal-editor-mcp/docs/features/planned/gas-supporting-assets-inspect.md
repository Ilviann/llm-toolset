---
feature_id: gas-supporting-assets-inspect
status: planned
depends_on:
  - gas-ability-blueprints-inspect
  - gas-gameplay-effects-inspect
  - companion-asset-adapters
released_in: null
---

# `gas-supporting-assets-inspect` — Supporting Gameplay Ability System asset inspection

**Outcome:** `asset_inspect` extends the released Gameplay Ability and Gameplay Effect coverage to standalone Gameplay Cue Notify, Attribute Set, magnitude-calculation, and execution-calculation Blueprint assets.

**Current coverage:** Gameplay Ability Blueprints and Gameplay Effect Blueprints are already released through `asset_inspect`. Gameplay Effect inspection already reports cue entries embedded in an Effect; those records are not standalone Gameplay Cue assets and are not duplicated by this feature.

**Depends on:**

- [`gas-ability-blueprints-inspect`](../completed/gas-ability-blueprints-inspect.md)
- [`gas-gameplay-effects-inspect`](../completed/gas-gameplay-effects-inspect.md)
- [`companion-asset-adapters`](../completed/companion-asset-adapters.md)

### Asset-family scope

- Extend the existing optional `UnrealMCPGAS` companion and the released `asset_inspect` family-adapter contract. Add no GAS-specific model-facing tool and preserve all current Ability/Effect output and unavailable-state behavior.
- Inspect exact Blueprint assets derived from `UGameplayCueNotify_Static` or `AGameplayCueNotify_Actor`, including supported Burst, Burst Latent, Looping, and Hit Impact descendants. Compose ordinary Blueprint defaults, members, graphs, events, references, and diagnostics with cue tags, event-response policy, recycling/uniqueness, timing, placement, effect/audio/animation references, and stable GAS-specific selectors where public UE 5.8 APIs expose persisted state.
- Inspect exact `UAttributeSet` Blueprint assets with declared gameplay attributes, replicated/property policy, defaults, inheritance, attribute identities, clamping or metadata references where explicitly persisted, supported callbacks/graphs, and references from released Ability/Effect records. Do not infer replication or clamping behavior from naming conventions.
- Inspect exact `UGameplayModMagnitudeCalculation` and `UGameplayEffectExecutionCalculation` Blueprint assets by composing ordinary Blueprint semantics with captured-attribute definitions, tag requirements, calculation policy, supported defaults, and references used by Gameplay Effects. Preserve unresolved native or Blueprint calculation classes as typed references.
- Bound attributes, captures, cue records, graphs, references, nested values, diagnostics, response size, and Game-thread time. Unknown GAS subclasses remain explicit unsupported records rather than being silently treated as ordinary Blueprints.

### Exclusions and completion gate

- `UAbilityTask` is not a Blueprintable standalone asset base in UE 5.8; Ability Task usage remains graph-node semantics inside Gameplay Ability inspection. Do not advertise native task classes as project assets. Also exclude runtime Ability System Components, granted abilities, Gameplay Effect Specs, attribute values, cue dispatch, prediction, replication state, supplied C++, or arbitrary task/calculation execution.
- Verify static and actor Cue Notify variants, Attribute Set inheritance and attributes, magnitude and execution calculations, cross-references from Abilities and Effects, unresolved classes/assets/tags, unavailable companion states, bounds, deterministic snapshots, restart stability, and non-mutation.
- Complete only after focused and full Python tests, UE 5.8 public-header probes and native Automation, production-socket headless integration, adaptive/forced-unity/non-unity editor builds, and isolated base/GAS Win64 packaging pass. Record remaining applicable macOS verification in the roadmap backlog.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
