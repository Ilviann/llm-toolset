# Asset inspection service

## Ownership

`FUnrealMCPAssetInspectionService` owns request decoding, exact `/Game` target resolution, canonical object paths, frozen primary/overlay family selection, request orchestration, composition collision checks, stable-snapshot checks, read-only preservation, and final typed-document encoding. `UnrealMCPAssetInspectionAdapters.cpp` owns neutral/media and core Blueprint classification plus focused Interface, standalone Actor Component, gameplay/Data Asset/Widget Blueprint, graph, collection, and semantic-property inspection behavior. `UnrealMCPDataInspectionAdapters.cpp` owns Data Asset instance and Data Table families; the UMG domain owns the base Widget Blueprint semantic overlay. Admitted companions own only their semantic blocks, routes, and fingerprint material. Python owns the exact MCP schema and final deterministic safe-YAML rendering.

## Dependency direction

The built-in domains register neutral, Blueprint, Data Asset, and Data Table descriptors, admit compatible companion families, then freeze the registry before bridge construction. The command catalog injects that same registry into the Game-thread service. The coordinator depends on Asset Registry resolution, typed family contexts/builders, and adapter-owned snapshot collaborators; built-in and companion adapters depend on their narrow domain collectors. No adapter owns MCP framing, editor UI, transactions, compilation, saving, media payload APIs, world runtime state, or project-wide discovery.

## Invariants

- Only exact `/Game` package or matching object paths are accepted; both normalize to one object path. Filesystem syntax, traversal, backslashes, alternate mounts, and mismatched object names reject before loading.
- Every result contains canonical asset identity and one query-independent snapshot. Built-in deep families are Actor/gameplay-framework/Data Asset/Widget Blueprints, standalone Actor Component Blueprints, Blueprint Interfaces, Data Asset instances, and Data Tables; all other families receive bounded neutral identity and explicit limitations.
- `UBlueprint` storage and derived Blueprint asset classes select the `core_blueprint` descriptor; all other `UObject`-derived assets select the lower-priority `neutral_asset` fallback unless a more specific built-in descriptor is registered. Family-specific classification and semantic collection never occur in the coordinator.
- The service derives a Blueprint's represented class for companion matching, selects every admitted inspection overlay, and composes them without replacing the storage-family result. Unavailable or rejected companions therefore leave the primary response unchanged.
- Root calls run the primary adapter and every matching overlay, omit only the primary's now-obsolete limitation block, merge declared selectors in stable order, and include all snapshot contributions. A targeted companion selector invokes only its owning overlay while preserving base identity and the cumulative query-independent snapshot.
- Root records are compact and advertise exact percent-encoded selectors. Non-graph collections page deterministically from zero with default 10 and maximum 100 records.
- Selected graphs traverse live `UEdGraph` objects. Response-local semantic node IDs are query-local; edges appear once from source output pins. Verbose mode adds native graph/node/pin identities and coordinates without changing semantics.
- Graphs are atomic. Complete output is bounded to 64 KiB; oversized graphs return `data_limit_exceeded` unless the caller explicitly permits one coherent marked slice.
- Inspection never returns media, retrieval paths, runtime state, discovery results, or reconstruction boundaries, and it verifies package dirtiness, Blueprint status, and snapshot stability before returning.

## Verification

`UnrealMCP.AssetInspect.CoreFamiliesSelectorsPagingAndLimits` covers canonical paths, every core classification including Widget and Data Asset Blueprints, neutral/media behavior, selectors, UTF-8 encoding, paging, Interface declarations, snapshots, invalid/security cases, non-mutation, and complete/partial graph limits through registry-backed dispatch. `UnrealMCP.AssetInspect.DataAssetsTablesSelectorsAndSnapshots` and `UnrealMCP.AssetInspect.UMGHierarchyLayoutBindingsAndExclusions` cover the focused structured-data and UMG overlays. Companion Automation covers overlay admission, route collisions, exact blocks, cumulative snapshots, repeatability, and unchanged state. Python tests cover exact family-set policy, schemas, deterministic YAML, strict types/escaping, and UTF-8 stdio. Lifecycle acceptance exercises built-in and companion root/selector reads through MCP YAML framing across restart.
