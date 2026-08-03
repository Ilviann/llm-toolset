# Companion extension registry

The base `UnrealMCP` editor module owns companion discovery, admission, registry lifetime, capability composition, and dispatch. Public companion-facing declarations live in `Public/IUnrealMCPModule.h` and `Public/UnrealMCPCompanionApi.h`; `Private/UnrealMCPExtensionRegistry.*` owns policy. Companions never own transport, authentication, queues, credentials, the operation ledger, or model-facing schemas.

## Startup and ownership

During base-module startup the registry scans bounded `.uplugin` metadata through Unreal's plugin manager. It ignores ordinary plugins, records disabled companions without loading them, validates exact descriptor API/schema identity and dependencies, and loads only an enabled declared owning module. That module makes one aggregate `RegisterCompanion` call. The registry validates compiled identity, handler readiness, limits, loaded module ownership, and collisions, sorts admitted records, and freezes before bridge readiness. Enablement changes, hot reload, replacement, and late registration require an editor restart. Shutdown closes admission before the bridge stops and allows only owner-matched unregister calls.

The base remains usable when a companion is absent or rejected. `capabilities` publishes bounded deterministic companion records and unavailable reasons; it never publishes paths, credentials, addresses, or runtime-provided schemas.

## Dispatch boundary

The Python server owns an exact-version allowlist in `unreal_editor_mcp/extension_catalog.py`. It intersects those schemas with ready native contributions and the immutable startup access mode. Read contributions may augment existing inspection tools in readonly mode; mutation branches appear only with `--writable`. Unknown, stale, mismatched, or forged extension operations fail schema validation before target dispatch.

Accepted requests still use the base authenticated route, bounded queue, Game-thread dispatch, mutation ledger, target and mount checks, stale snapshots, editor transactions, postcondition read-back, rollback verification, and stable protocol errors. The companion handler may narrow validation and operate only on its declared target; it cannot broaden base policy.

An admitted read-only `AssetFamily` contribution for `blueprint_inspect` may classify a new Blueprint family through its existing target-family/class policy. The base inspector still owns discovery and ordinary Blueprint collection, calls the typed handler on the generated-class default object, incorporates the companion fingerprint into its snapshot, and appends only bounded companion records. This integration uses companion API v1 unchanged.

## Implementation and verification

`plugin/UnrealMCPTestCompanion/` is an independently versioned, disabled-by-default editor-only fixture. It registers read and mutation pairs for a new UObject asset family, a Blueprint component family, and an existing Actor Blueprint contribution. `plugin/UnrealMCPGAS/` is the first released companion and registers inspection-only Gameplay Ability and Gameplay Effect Blueprint families. `UnrealMCP.Companions.AuthenticatedBridgeRoundTrip`, `UnrealMCP.Companions.BlueprintFamilyInspectionIntegration`, GAS Automation, Python schema/server contracts, headless companion checks, public API probes, builds, and packaging cover the boundary.

[Types and author contract](../types/companion-extension-registry/index.md) · [User guide](../user/companion-plugins.md) · [Architecture index](index.md)
