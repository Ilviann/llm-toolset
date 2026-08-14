# Companion extension registry

The base `UnrealMCP` editor module owns companion discovery, admission, registry lifetime, capability composition, and dispatch. Public companion-facing declarations live in `Public/IUnrealMCPModule.h` and `Public/UnrealMCPCompanionApi.h`; `Private/UnrealMCPExtensionRegistry.*` owns policy. Companions never own transport, authentication, queues, credentials, the operation ledger, model-facing schemas, transactions, persistence authority, or response encoding.

## Startup and ownership

During base-module startup the registry scans at most 64 `.uplugin` descriptors through Unreal's plugin manager. It ignores ordinary plugins, records disabled companions without loading them, validates exact descriptor API/schema identity and dependencies, and loads only an enabled declared owning module. Companion modules use `LoadingPhase: None`, making the initialized base registry their sole startup loader. The loaded module makes one aggregate `RegisterCompanion` call. The registry validates compiled identity, handler and adapter shape, limits, loaded module ownership, family/contribution collisions, selector routes, stable nested-identity kinds, and persistence declarations before sorting and freezing admitted records.

The current global `companion_api_version` and extension schema revision are both `2`. Descriptor, companion binary, base binary, and Python catalog values must agree exactly; missing, v1, stale, ranged, or mixed installations fail closed. Enablement changes, hot reload, replacement, and late registration require an editor restart. Shutdown closes admission before the bridge stops and permits only owner-matched unregister calls.

The base remains usable when a companion is absent or rejected. `capabilities` publishes bounded deterministic companion records and unavailable reasons; it never publishes paths, credentials, addresses, or runtime-provided schemas.

## Typed family and dispatch boundaries

Companion API v2 uses `FUnrealMCPRecord` and `FUnrealMCPValue` across the handler boundary. `FJsonObject` is absent from the public API and from every shipped companion. `FUnrealMCPCompanionAssetFamily` provides one bounded foreseeable seam for exact/derived classification, root and selector inspection, target-free creation, existing-target editing, stable nested identities, snapshot contribution, postcondition read-back, persistence requirements, typed capabilities, limits, and domain adapters.

The base validates and freezes those contracts. It retains exact target resolution, access policy, authentication, Game-thread dispatch, operation-ledger handling, package and mount checks, stale snapshots, editor transactions, compile/save policy, rollback verification, capability composition, collision policy, and JSON encoding. A companion can narrow its declared domain operation but cannot broaden base policy or contribute a dynamic model-facing schema.

The Python server owns the exact-version allowlist in `unreal_editor_mcp/asset_family_catalog.py`; `extension_catalog.py` retains compatibility exports only. The catalog validates bounded API-v2 family capability records and intersects shipped schemas with ready native contributions and immutable startup access mode. Unknown, malformed, stale, or forged records publish no operation branch.

The released GAS and CommonUI collectors retain their current capability-visible Blueprint inspection behavior after migrating to typed v2 records. Their records remain outside `asset_inspect` until [`companion-asset-adapters`](../features/planned/companion-asset-adapters.md) routes approved families through the common facade. No new GAS or CommonUI mutation is published by this migration.

## Implementation and verification

`plugin/UnrealMCPTestCompanion/` is the independently versioned disabled-by-default fixture. It exercises typed read and mutation contributions plus API-v2 admission, capability/adapter agreement, selector routes, target-free creation, existing-target editing, persistence declarations, and stable nested identities. `plugin/UnrealMCPGAS/` and `plugin/UnrealMCPCommonUI/` use the same JSON-neutral handler contract while keeping optional Engine dependencies outside the base.

Native admission, public API probes, companion Automation, Python catalog and server contracts, adaptive/forced-unity/non-unity builds, production-socket headless checks, and separate base/GAS/CommonUI/fixture packaging qualify the boundary on Windows.

[Types and author contract](../types/companion-extension-registry/index.md) · [User guide](../user/companion-plugins.md) · [Architecture index](index.md)
