# Enhanced Input asset inspection

## Ownership and boundaries

`plugin/UnrealMCPEnhancedInput/` owns every direct `EnhancedInput` and `InputCore` dependency. Its editor module registers five inspection-only companion API v2 families for Input Actions, Mapping Contexts, legacy player-mappable configs, and custom trigger/modifier Blueprints. The base plugin retains exact asset resolution, base Data Asset or Blueprint inspection, selector composition, snapshots, bounds, non-mutation checks, transport, and safe-YAML rendering.

The companion reads persisted assets and generated-class defaults only. It uses public `UInputAction`, `UInputMappingContext`, `FEnhancedActionKeyMapping`, `UPlayerMappableKeySettings`, and legacy config accessors. Inline trigger/modifier settings pass through one fixed persisted-property allowlist. Unknown plugin subclasses and custom fields are explicit bounded unsupported records; callers cannot provide reflection paths.

## Inspection flow

The companion registry admits `UnrealMCPEnhancedInput` only when the project effectively enables the Engine Enhanced Input plugin, the `EnhancedInput` module is loaded, descriptor and compiled identities agree, and API/schema values exactly match. The three direct asset families match exact asset classes. Trigger and modifier families match Blueprint-generated descendants of their native bases and compose with ordinary Blueprint graph/default inspection.

Action records preserve trigger/modifier order. Mapping Context records preserve default and per-profile mapping order, repeated actions and keys, action resolution, key names, effective player-mappable settings, and nested object order. Stable identities include the owning asset/profile/index and referenced action/key so repeated mappings remain distinct. Every complete bounded semantic projection contributes to one query-independent snapshot.

## Capability and mutation policy

Capabilities publish exactly five read-only families, one selector per family, stable mapping/trigger/modifier identity kinds, and fixed limits only while the companion is ready. The companion registers no creation or editing adapter and never executes `UpdateState`, `ModifyRaw`, input injection, user-settings queries, mapping rebuilds, project-setting mutation, compilation, or save operations.

`UPlayerMappableInputConfig` remains inspectable for migration work but is marked deprecated in every UE 5.8 record. Runtime `UEnhancedInputUserSettings` objects are not treated as assets.

## Verification

`UnrealMCP.EnhancedInput.AssetInspection` covers action, context, nested built-ins, legacy deprecation, deterministic fingerprints, and package-dirtiness preservation. `UnrealMCP.EnhancedInput.LiveFixture` persists all five asset families. Python catalog and production-socket restart checks verify exact admission, root/selector composition, safe repeated reads, and restart-stable snapshots.

[Wire contracts](../types/enhanced-input-asset-inspection/index.md) · [User guide](../user/enhanced-input-assets.md) · [Architecture index](index.md)
