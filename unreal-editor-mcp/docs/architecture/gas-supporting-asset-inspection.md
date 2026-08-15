# Supporting GAS asset inspection

## Ownership and boundaries

`plugin/UnrealMCPGAS/` owns five read-only API-v2 overlay families for Gameplay Cue Notify Static, Gameplay Cue Notify Actor, Attribute Set, Gameplay Mod Magnitude Calculation, and Gameplay Effect Execution Calculation Blueprint assets. The base plugin remains free of direct Gameplay Ability System dependencies and owns exact asset resolution, ordinary Blueprint logic/default inspection, selector composition, snapshots, limits, non-mutation checks, transport, and YAML rendering.

The companion reads verified generated-class default objects only. Cue properties enter through a fixed UE 5.8 allowlist and bounded traversal of those values. Attribute discovery uses `UAttributeSet::GetAttributesFromSetClass`; calculation captures and policies use public Gameplay Ability System accessors. Callers cannot supply property paths, execute calculations or cues, inspect runtime Ability System Components, or reach arbitrary reflected layouts.

## Inspection flow

The static and actor Cue roots are separate native families because Unreal exposes unrelated `UObject` and `AActor` bases; both compose the stable `gameplay_cue_notify` selector. Their block reports Cue kind, tag/name, override and handled-event policy, bounded persisted settings, and hard/soft object, class, audio, animation, effect, and placement references found under the allowlisted properties.

`attribute_set` reports inherited and locally declared gameplay attributes with stable property identities, owner classes, numeric defaults, `FGameplayAttributeData` base/current values, exact replication and RepNotify flags, and explicit clamp metadata. The magnitude- and execution-calculation selectors report resolved/unresolved captured attributes, capture source/snapshot policy, duplicate identities, magnitude dependency policy, execution passed-tag/scoped-modifier policy, and valid transient aggregator tags.

Every overlay contributes its complete bounded semantic state to the common query-independent snapshot. Root and selected reads therefore remain repeatable and stale-safe without loading unresolved soft targets or changing package dirtiness.

## Capability and mutation policy

The five families appear only when the optional `UnrealMCPGAS` 0.4.0 companion, Engine Gameplay Abilities plugin/module, companion API v2, schema revision 2, and exact Python family catalog agree. Each family publishes inspection true and creation/editing false. Missing or rejected GAS support leaves the base Blueprint result unchanged.

Ability Tasks remain graph-node semantics inside Gameplay Ability Blueprints. Runtime attribute values, granted abilities, Gameplay Effect Specs, cue dispatch, prediction, replication state, supplied C++, arbitrary reflection, creation, compilation, saving, and mutation are excluded.

## Verification

`UnrealMCP.GAS.SupportingAssetInspection` covers all five class roots, Attribute Set declarations, root/selector composition, deterministic snapshots, and package-dirtiness preservation. The persistent GAS fixture set exercises every new family through production-socket `asset_inspect` after restart. Python catalog and contract tests cover exact seven-family companion admission, read-only operations, bounds, and unchanged shared schemas.

[Wire contracts](../types/gas-supporting-asset-inspection/index.md) · [User guide](../user/gas-supporting-assets.md) · [Architecture index](index.md)
