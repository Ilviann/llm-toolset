# Companion extension contracts

## Native entry points

`IUnrealMCPModule` exposes `GetCompanionApiVersion()`, aggregate `RegisterCompanion(registration, owning_module)`, and owner-checked `UnregisterCompanion(handle, owning_module)`. `FUnrealMCPCompanionRegistration` carries plugin, extension, owning-module, independent semantic-version, compiled API version, schema revision, Engine dependencies, asset-family definitions, and existing typed contribution records. The globally exact API version and extension-schema revision are both `2`; semantic versions remain independent.

## Typed records and handlers

`IUnrealMCPExtensionHandler` receives and returns only bounded `FUnrealMCPRecord` and `FUnrealMCPValue` trees. It provides readiness, exact-target support, argument validation, inspection, companion-owned fingerprint material, mutation, and read-back callbacks. Handlers receive loaded objects only after base validation and run on the Game thread. `FUnrealMCPExtensionError` carries typed details. Neither the public header nor a shipped companion depends on Unreal's JSON module.

Existing `FUnrealMCPExtensionContribution` records keep one stable ID, category (`AssetFamily`, `ComponentFamily`, or `ExistingAssetContributor`), access (`Read` or `Mutation`), persistence (`None`, `PackageSave`, or `BlueprintCompileAndSave`), base tool family, operation, target family/class policy, required live capability, argument-field allowlist, stable limits, and typed handler. They remain available for compatible non-family extensions and the fixture; released GAS/CommonUI inspection now uses asset-family definitions.

## Asset-family definitions

`FUnrealMCPCompanionAssetFamily` declares stable family and native-class identities, exact or derived class policy, priority, required modules, common document/selector/snapshot bounds, named limits, independent inspection/creation/editing capabilities, selector routes, stable nested-identity kinds, creation/editing persistence, typed adapters, and a snapshot builder.

Capability booleans must agree exactly with adapter presence. A family must expose at least one capability. Class-policy collisions, duplicate family or selector identities, malformed limits, duplicate nested-identity kinds, invalid persistence, missing modules, and over-limit records fail the entire companion registration. Admitted family definitions are converted to common inspection overlays and registered atomically before the shared family registry freezes. Target-free creation uses `FUnrealMCPAssetFamilyCreationContext`; existing-target edits use `FUnrealMCPAssetFamilyEditContext`; both return typed read-back and snapshot material through the common adapter contracts.

## Descriptor and capability records

A companion descriptor has top-level `companion_api_version` and an `unreal_mcp_companion` object containing `extension_id`, `schema_revision`, `owning_module`, and `required_engine_plugins`. It declares an enabled unpinned dependency on `UnrealMCP`.

Native `capabilities` adds global API/schema values, registry signature, ordered companions, and bounded diagnostics. Each companion includes deterministic `asset_families` and `contributions`. Family records publish class policy, supported operations, selector identities, stable nested-identity kinds, persistence, and limits. Python validates this shape before setting `effective_ready`; it never accepts runtime schemas.

## Bounds and errors

Discovery is limited to 64 descriptors, 16 admitted companions, 16 asset families and 32 contributions per companion, 128 total capability records, 32 argument fields and named limits per contribution, 64-character stable IDs, and 64 diagnostics. Common family bounds further limit document records/bytes, recursive values/depth, selector routes/segments, and snapshot contributions/bytes. Relevant stable failures include `extension_unavailable`, `invalid_asset_family`, `asset_family_collision`, `invalid_argument`, `invalid_path`, `invalid_asset`, `mutation_scope_denied`, `stale_precondition`, and `rollback_failed`.

[Owning component](../../architecture/companion-extension-registry.md) · [Types index](../index.md)
