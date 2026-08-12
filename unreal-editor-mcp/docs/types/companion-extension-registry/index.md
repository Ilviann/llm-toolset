# Companion extension contracts

## Native entry points

`IUnrealMCPModule` exposes only `GetCompanionApiVersion()`, one aggregate `RegisterCompanion(registration, owning_module)`, and owner-checked `UnregisterCompanion(handle, owning_module)`. `FUnrealMCPCompanionRegistration` carries plugin, extension, owning-module, independent semantic-version, compiled API version, schema revision, Engine dependency, and contribution records. The globally exact API version and initial extension-schema revision are both `1`; semantic versions remain independent.

## Contributions and handlers

Each `FUnrealMCPExtensionContribution` has one stable ID, category (`AssetFamily`, `ComponentFamily`, or `ExistingAssetContributor`), access (`Read` or `Mutation`), persistence (`None`, `PackageSave`, or `BlueprintCompileAndSave`), existing base tool family, exact operation, target family/class policy, required live capability, argument-field allowlist, stable limits, and handler. Save policies refuse an already-dirty package, run postcondition read-back before persistence, and restore the transaction if saving fails.

`IUnrealMCPExtensionHandler` provides readiness, exact target support, argument validation, inspection, companion-owned fingerprint material, mutation, and read-back callbacks. Handlers receive loaded objects only after base validation and always run on the Game thread. Model input remains untrusted even though companions are trusted native project code.

Companion API v1 retains its historical read-only Blueprint `AssetFamily` contribution records and handlers, but 0.36.0 no longer publishes their former model-facing inspection route. They remain dormant internal registrations until a separately approved companion API/read-facade design integrates them without changing `asset-inspect-core`. Mutation contributions remain independently admitted under their existing contracts.

## Descriptor and capability records

A companion descriptor has top-level `companion_api_version` and an `unreal_mcp_companion` object containing `extension_id`, `schema_revision`, `owning_module`, and `required_engine_plugins`. It must declare an enabled dependency on `UnrealMCP`; the base dependency has no semantic-version pin.

Native `capabilities` adds `companion_api_version`, `extension_schema_revision`, `extension_registry_signature`, ordered `companions`, and bounded diagnostics. Python annotates `python_known`, `effective_ready`, and `effective_unavailable_reason` after exact catalog intersection.

## Bounds and errors

Discovery is limited to 64 descriptors, 16 admitted companions, 32 contributions per companion, 32 argument fields and stable limits per contribution, 64-character stable IDs, and 64 diagnostics. Relevant stable failures include `extension_unavailable`, `invalid_argument`, `invalid_path`, `invalid_asset`, `mutation_scope_denied`, `stale_precondition`, and `rollback_failed`.

[Owning component](../../architecture/companion-extension-registry.md) · [Types index](../index.md)
