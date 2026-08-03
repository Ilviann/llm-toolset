# Gameplay Ability Blueprint inspection

## Ownership and boundaries

`plugin/UnrealMCPGAS/` owns every direct dependency on the Engine Gameplay Ability System plugin and the `GameplayAbilities`, `GameplayTags`, and `GameplayTasks` modules. Its editor module registers one read-only `gameplay_ability` Blueprint asset-family contribution through companion API v1. The base `UnrealMCP` plugin remains GAS-free and owns discovery, authentication, Game-thread dispatch, Blueprint structure collection, pagination, snapshots, limits, and capability composition.

The companion reads only `UGameplayAbility` class default objects reached from verified Gameplay Ability Blueprints. It uses public policy getters plus an exact allowlist of typed GAS properties; it does not accept property paths or unrestricted reflection. `blueprint_inspect` remains the only model-facing tool.

## Inspection flow

When the companion is admitted, the extension registry classifies native and Blueprint-generated `UGameplayAbility` descendants as `gameplay_ability`. The base inspector collects ordinary summary, default, member, graph, node, pin, diagnostic, and reference records. The companion appends four records selected by the `gameplay_ability` section: policies, tag containers, triggers, and cost/cooldown Gameplay Effect class references.

The companion fingerprint covers its full bounded typed state even when paged output is truncated or the typed section is omitted. The base combines that fingerprint with ordinary Blueprint structure into one snapshot and verifies that inspection did not change package dirtiness or compile state. Companion inspection records participate in the same page and response ceilings.

## Capability and mutation policy

`capabilities.blueprint_families` publishes `gameplay_ability` only while the exact companion/API/schema/dependency registration is ready. Its matrix enables discovery and inspection only. `features.gas_ability_blueprints_inspection` and `features.gas_ability_blueprints_mutation` distinguish read support from mutation support; this release reports mutation as false. Every existing Blueprint mutation service continues to use the base family policy and rejects Gameplay Ability assets.

The companion retains direct build dependencies on `GameplayAbilities`, `GameplayTags`, and `GameplayTasks`. Native admission uses the enabled Gameplay Abilities plugin plus its owning `GameplayAbilities` module as the live dependency gate because Unreal does not consistently register the linked tag and task runtime modules as independently loaded.

The companion is independently versioned at 0.1.0, requires global `companion_api_version: 1`, and can be packaged separately with `scripts/package_plugin.py --gas-companion`. The Windows deployment helper intentionally does not install it yet.

## Verification

`UnrealMCP.GAS.AbilityBlueprintInspection` covers typed policy/trigger read-back, deterministic fingerprints, and non-mutation. `UnrealMCP.Companions.BlueprintFamilyInspectionIntegration` covers the generic registry-to-standard-inspector seam. Python catalog tests cover exact readiness intersection and prove the companion adds no mutation schema.

[Wire contracts](../types/gas-ability-inspection/index.md) · [User guide](../user/gameplay-ability-blueprints.md) · [Architecture index](index.md)
