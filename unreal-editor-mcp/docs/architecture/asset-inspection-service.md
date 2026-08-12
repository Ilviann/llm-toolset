# Asset inspection service

## Ownership

`FUnrealMCPAssetInspectionService` owns request decoding, exact `/Game` target resolution, canonical object paths, frozen family selection, request orchestration, stable-snapshot checks, read-only preservation, and final typed-document encoding. `UnrealMCPAssetInspectionAdapters.cpp` owns neutral/media and core Blueprint classification plus focused Interface, standalone Actor Component, gameplay Blueprint, graph, collection, and semantic-property inspection behavior. Python owns the exact MCP schema and final deterministic safe-YAML rendering.

## Dependency direction

The module registers built-in neutral and Blueprint inspection descriptors, then freezes the registry before bridge construction. The command catalog injects that same registry into the Game-thread service. The coordinator depends on Asset Registry resolution, typed family contexts/builders, and an adapter-owned stable-snapshot collaborator; adapters depend on shared protocol errors, property/K2 codecs, Blueprint structural fingerprinting, SCS, reflection, and direct `UEdGraph` APIs. Neither layer depends on MCP framing, companion handlers, editor UI, transactions, compilation, saving, media payload APIs, world runtime state, or project-wide discovery.

## Invariants

- Only exact `/Game` package or matching object paths are accepted; both normalize to one object path. Filesystem syntax, traversal, backslashes, alternate mounts, and mismatched object names reject before loading.
- Every result contains canonical asset identity and one query-independent snapshot. Core deep families are Actor/gameplay-framework Blueprints, standalone Actor Component Blueprints, and Blueprint Interfaces; all other families receive bounded neutral identity and explicit limitations.
- `UBlueprint` storage and derived Blueprint asset classes select the `core_blueprint` descriptor; all other `UObject`-derived assets select the lower-priority `neutral_asset` fallback unless a more specific built-in descriptor is registered. Family-specific classification and semantic collection never occur in the coordinator.
- Root records are compact and advertise exact percent-encoded selectors. Non-graph collections page deterministically from zero with default 10 and maximum 100 records.
- Selected graphs traverse live `UEdGraph` objects. Response-local semantic node IDs are query-local; edges appear once from source output pins. Verbose mode adds native graph/node/pin identities and coordinates without changing semantics.
- Graphs are atomic. Complete output is bounded to 64 KiB; oversized graphs return `data_limit_exceeded` unless the caller explicitly permits one coherent marked slice.
- Inspection never returns media, retrieval paths, runtime state, discovery results, or reconstruction boundaries, and it verifies package dirtiness, Blueprint status, and snapshot stability before returning.

## Verification

`UnrealMCP.AssetInspect.CoreFamiliesSelectorsPagingAndLimits` covers canonical paths, every core classification, neutral/media behavior, selectors, UTF-8 encoding, paging, Interface declarations, snapshots, invalid/security cases, non-mutation, and complete/partial graph limits through registry-backed dispatch. `UnrealMCP.AssetInspect.AdapterIsolation` proves an exact family overrides only its neutral fallback while core Blueprint and unrelated-family selection remain unchanged. Python tests cover schemas, catalog removal, deterministic YAML, strict types/escaping, and UTF-8 stdio. Lifecycle acceptance exercises MCP YAML framing and semantic snapshots across restart.
