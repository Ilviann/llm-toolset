# Built-in asset-family registry

## Ownership

`FUnrealMCPAssetFamilyRegistry` owns trusted base-plugin family descriptors, deterministic class selection, required-module readiness, independent inspection/creation/editing capability admission, stable ordering, registry fingerprints, and freeze state. `FUnrealMCPAssetFamilyDocumentBuilder`, `FUnrealMCPAssetFamilySelectorRouter`, and `FUnrealMCPAssetFamilySnapshotBuilder` own adapter-local bounds, collision checks, deterministic selector routing, and ordered snapshot contributions.

## Dependency direction

The module creates and freezes the built-in registry before constructing the bridge. The command catalog retains the frozen registry for later domain composition and refuses startup composition with a mutable registry. Family adapters depend inward on typed contexts, semantic value records, and bounded builders; they do not depend on the bridge, JSON codecs, Python schemas, access policy, target resolution, transactions, persistence, or response encoding.

`asset_inspect` now selects the frozen `core_blueprint` or `neutral_asset` descriptor and invokes its inspection adapter. Existing authoring services retain their released paths until the authoring-kernel feature migrates them. Registry-backed inspection changes no MCP command, schema, capability envelope, or companion API.

## Invariants

- Family IDs and limit IDs are bounded stable lowercase identities; native classes are resolved `UClass` values with exact or exact-and-derived matching.
- Higher priority wins. Multiple matching descriptors at the same highest priority fail with `ambiguous_classification`; registration rejects an identical class-policy-priority claim early.
- Required module names are captured at freeze. Missing dependencies leave the descriptor visible to trusted native composition but reject selection with `dependency_unavailable`.
- Inspection, creation, and editing are independent declarations. Each Boolean must agree exactly with the presence of its typed adapter.
- Registration order cannot affect frozen ordering or the registry fingerprint. Descriptor bounds, limits, modules, class policy, priority, and capability shape all participate in the fingerprint.
- Semantic paths and types, selector routes, snapshot identities, record/value counts and depth, computed value bytes, route depth, and contribution counts are bounded. Duplicate semantic paths, selector claims, and snapshot identities fail closed.
- Target resolution, writable-mount/access policy, request schemas, mutation replay, transactions, persistence and rollback authority, wire codecs, and final response limits remain outside adapters.

## Verification

`UnrealMCP.AssetFamilies.RegistrySelectionCapabilitiesAndFreeze` covers exact and derived selection, priority, ambiguity, missing dependencies, descriptor collisions, capability mismatch, independent capability combinations, freeze refusal, ordering, and restart-deterministic fingerprints. `UnrealMCP.AssetFamilies.BoundedBuildersAndSyntheticAdapter` drives a synthetic inspection adapter through semantic document, selector, and snapshot contracts and covers their limits and collisions. `UnrealMCPApiProbe.cpp` keeps the registry descriptor and three typed adapter contexts in the normal UE 5.8 build boundary.
