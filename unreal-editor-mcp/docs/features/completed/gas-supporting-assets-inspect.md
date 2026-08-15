---
feature_id: gas-supporting-assets-inspect
status: completed
depends_on:
  - gas-ability-blueprints-inspect
  - gas-gameplay-effects-inspect
  - companion-asset-adapters
released_in: "0.50.0"
---

# `gas-supporting-assets-inspect` — Supporting Gameplay Ability System asset inspection

**Outcome:** `asset_inspect` extends the released Gameplay Ability and Gameplay Effect coverage to standalone Gameplay Cue Notify, Attribute Set, magnitude-calculation, and execution-calculation Blueprint assets.

**Implementation status:** Completed in 0.50.0 with `UnrealMCPGAS` 0.4.0 on unchanged companion API v2 and schema revision 2. Five exact read-only companion overlay families compose their semantic blocks with ordinary Blueprint inspection while an absent or rejected companion leaves the base result unchanged.

**Depends on:**

- [`gas-ability-blueprints-inspect`](gas-ability-blueprints-inspect.md)
- [`gas-gameplay-effects-inspect`](gas-gameplay-effects-inspect.md)
- [`companion-asset-adapters`](companion-asset-adapters.md)

### Released asset-family contract

- Separate exact-and-derived static and actor Cue families share selector `gameplay_cue_notify`. They report supported Static, Actor, Burst, Burst Latent, Looping, and Hit Impact specialization, Cue tag/name, override and handled-event policy, allowlisted persisted cleanup/uniqueness/timing/placement/effect settings, and bounded hard/class/soft references without loading unresolved targets.
- `attribute_set` reports every public-collected gameplay attribute with stable property identity, local/inherited ownership, owner class/asset, numeric or `FGameplayAttributeData` base/current defaults, exact Net/RepNotify state, and only explicit clamp/UI metadata. It does not infer replication or clamping from names.
- `gameplay_mod_magnitude_calculation` and `gameplay_effect_execution_calculation` report captured-attribute identities, source/target and snapshot policy, duplicates, unresolved references, magnitude dependency registration, execution passed-tag and scoped-modifier policy, and valid transient aggregator tags.
- All five families are inspection-only and use the shared exact-path, selector, deterministic document, cumulative snapshot, non-mutation, companion availability, and safe-YAML contracts. Existing Gameplay Ability and Gameplay Effect output is unchanged.

### Bounds and exclusions

- Per-family ceilings are four adapter records, 64 persisted properties, 128 references, 256 attributes, 128 captures, 2,048 traversed values, 4,096 bytes per exported property, depth 8, 65,536 value nodes, and four megabytes. Overflow fails closed; GAS selectors reject paging and graph flags rather than ignoring them.
- `UAbilityTask` remains graph-node semantics inside Gameplay Ability inspection. Runtime Ability System Components, granted abilities/specs, live attribute values, Cue dispatch, calculation execution, prediction, replication state, supplied C++, arbitrary reflection, creation, compilation, saving, and mutation remain excluded.

### Verification

UE 5.8 public headers established all class/property/accessor boundaries. Native Automation covers every class root, Attribute Set declaration, selectors, snapshots, and non-mutation. Persistent fixtures exercise every family through production-socket inspection after restart. Python catalog/contract tests cover exact companion admission and schemas; Windows adaptive, forced-unity, non-unity, full Automation, lifecycle, and isolated base/GAS packaging gates complete the release. macOS verification remains preferred follow-up work.

[Back to roadmap](../../../ROADMAP.md) · [Wire contracts](../../types/gas-supporting-asset-inspection/index.md) · [User guide](../../user/gas-supporting-assets.md)
