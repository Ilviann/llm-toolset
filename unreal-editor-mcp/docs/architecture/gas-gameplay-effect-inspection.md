# Gameplay Effect inspection

## Ownership and boundaries

`plugin/UnrealMCPGAS/` owns the read-only `gameplay_effect` Blueprint asset-family contribution and every direct dependency on the Engine Gameplay Ability System modules. It registers through companion API v1 beside the existing `gameplay_ability` contribution. The base plugin remains GAS-free and continues to own discovery, authentication, Game-thread dispatch, Blueprint inspection, pagination, snapshots, limits, and capability composition.

The companion reads only verified `UGameplayEffect` class default objects. It uses public Unreal Engine 5.8 members plus exact reflected names for documented public fields whose containers need typed access. It never accepts caller-supplied property paths, walks project-defined object layouts, creates tags, evaluates magnitudes, builds specs, or mutates an Ability System Component.

## Inspection flow

The frozen extension registry classifies usable native and Blueprint-generated `UGameplayEffect` descendants as `gameplay_effect` and retains eleven typed record collectors. In 0.36.0 their former shared model-facing route is removed; the collectors remain dormant pending a redesigned companion read contract.

Magnitudes are decoded only as scalable float, attribute based, custom calculation class, or set by caller. Components are accepted through an explicit public-class allowlist; unknown classes become typed unsupported records. Class, attribute, tag, curve, and asset references report resolution and compatibility without loading unrelated assets. Local/inherited ownership, stable nested identities, sorted values, duplicate detection, scan/output bounds, and bounded chained-effect traversal feed the same snapshot fingerprint even when output is paged or omitted.

## Capability and mutation policy

The family appears only when the exact companion/API/schema/dependency registration and Python catalog entry are ready. `features.gas_gameplay_effects_inspection` reports read support and `features.gas_gameplay_effects_mutation` remains false. The family matrix enables discovery and inspection only; component, member, graph, action-catalog, create, compile, save, and default-edit operations reject through existing base policy.

`UnrealMCPGAS` is independently versioned at 0.2.1. Global companion API v1 and schema revision 1 are unchanged; 0.2.1 changes only startup ordering and descriptor preservation.

## Verification

`UnrealMCP.GAS.GameplayEffectInspection` covers the four magnitude forms, typed components and references, cyclic chain reporting, deterministic fingerprints, and dirtiness preservation. `UnrealMCP.GAS.GameplayEffectLiveFixture` creates restart-persistent Gameplay Effect and Gameplay Ability cost-reference fixtures for production-socket inspection. Python catalog, contract, and headless tests cover exact admission, all eleven sections, repeatable snapshots, read-only rejects, and unchanged read-back.

[Wire contracts](../types/gas-gameplay-effect-inspection/index.md) · [User guide](../user/gameplay-effects.md) · [Architecture index](index.md)
