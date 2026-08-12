# Asset inspection service

## Ownership

`FUnrealMCPAssetInspectionService` owns exact `/Game` target resolution, canonical object paths, neutral and core-family classification, semantic root construction, selector routing, collection paging, graph traversal, snapshot checks, and read-only preservation. It uses Asset Registry lookup plus typed Blueprint, SCS, reflected-property, and direct `UEdGraph` APIs. Python owns the exact MCP schema and final deterministic safe-YAML rendering.

## Dependency direction

The bridge constructs and dispatches the service on the Game thread. The service depends on shared protocol errors, property/K2 codecs, and the internal Blueprint structural fingerprint used by authoring preconditions. It does not depend on MCP framing, companion handlers, editor UI, transactions, compilation, saving, media payload APIs, world runtime state, or project-wide discovery.

## Invariants

- Only exact `/Game` package or matching object paths are accepted; both normalize to one object path. Filesystem syntax, traversal, backslashes, alternate mounts, and mismatched object names reject before loading.
- Every result contains canonical asset identity and one query-independent snapshot. Core deep families are Actor/gameplay-framework Blueprints, standalone Actor Component Blueprints, and Blueprint Interfaces; all other families receive bounded neutral identity and explicit limitations.
- Root records are compact and advertise exact percent-encoded selectors. Non-graph collections page deterministically from zero with default 10 and maximum 100 records.
- Selected graphs traverse live `UEdGraph` objects. Response-local semantic node IDs are query-local; edges appear once from source output pins. Verbose mode adds native graph/node/pin identities and coordinates without changing semantics.
- Graphs are atomic. Complete output is bounded to 64 KiB; oversized graphs return `data_limit_exceeded` unless the caller explicitly permits one coherent marked slice.
- Inspection never returns media, retrieval paths, runtime state, discovery results, or reconstruction boundaries, and it verifies package dirtiness, Blueprint status, and snapshot stability before returning.

## Verification

`UnrealMCP.AssetInspect.CoreFamiliesSelectorsPagingAndLimits` covers canonical paths, every core classification, neutral/media behavior, selectors, UTF-8 encoding, paging, Interface declarations, snapshots, invalid/security cases, and complete/partial graph limits. Python tests cover schemas, catalog removal, deterministic YAML, strict types/escaping, and UTF-8 stdio. The lifecycle acceptance exercises MCP YAML framing and semantic snapshots across restart.
