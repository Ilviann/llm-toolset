---
feature_id: asset-inspect-core
status: completed
depends_on: []
released_in: "0.36.0"
---

# `asset-inspect-core` — General asset inspection foundation and gameplay Blueprints

**Outcome:** Agents can analyze ordinary Blueprint logic and framework configuration encapsulated in one exact Unreal asset through the small read-only `asset_inspect` API.

**Implementation status:** Completed in 0.36.0. Windows passed focused and full Python tests, adaptive/forced-unity/non-unity UE 5.8 builds, the full native Automation suite, complete headless cross-process lifecycle and restart validation, and isolated Win64 base-plugin packaging. macOS verification remains preferred follow-up work; Linux is out of scope.

The accepted shared request, YAML response, selector, graph, collection, inheritance, and family contracts are under [Asset inspection contracts](../../types/asset-inspection/index.md).

### Model-facing foundation

- Publish one read-only `asset_inspect` tool with required exact project-content `asset_path` and optional `selector`, `verbose`, `page_size`, `page_index`, and `allow_partial_graph` arguments. Normalize package and object path forms to one canonical object path.
- Return successful results as deterministic safe YAML 1.2-compatible text while retaining MCP JSON-RPC framing and shared structured tool errors. Keep native and Python records typed and JSON-compatible; render YAML only at the final model-facing boundary.
- Implement canonical percent-encoded hierarchical selectors, query-independent asset snapshots, zero-based deterministic collection paging, complete normalized graphs, verbose graph identities and coordinates, and the explicitly opted-in coherent oversized-graph fallback.
- Always return stable asset type identity. Include meaningful represented `parent_type` only when applicable, return type-only results for raw media families, and never emit media, bulk data, thumbnails, or media-retrieval paths.
- Keep `game_data_inspect` published. Remove the reconstruction-oriented Blueprint inspection tool after this semantic core covers its analysis role; retain only internal fingerprint support required by authoring preconditions.

### Core family scope

- Deeply inspect Actor Blueprints, Actor-owned component records, standalone Actor Component Blueprints, GameInstance, GameMode/GameModeBase, GameState/GameStateBase, PlayerController, PlayerState, and Blueprint Interfaces.
- Preserve important properties cumulatively across meaningful framework inheritance, adding derived-class properties once with declaring provenance.
- Return Blueprint variables, events, functions, macros, framework defaults, owned component semantics, callable signatures, normalized K2 nodes and links, and exact child selectors according to each supported family contract.
- Treat Blueprint Interfaces as declaration contracts: return callable signatures and metadata through non-graph selectors without fabricating implementation nodes or scanning for implementers and callers.
- Give unsupported and unknown asset families a bounded neutral identity/reflection result with explicit limitations. Later staged features add data, UMG, and Animation Blueprint deep inspection without changing the common tool shape.
- Exclude GAS routing and every companion-contributed `asset_inspect` block. Any future companion extension remains a separately approved API-version change.

### Implementation and verification

- Keep path validation, mounted-content confinement, classification, selector routing, bounds, snapshots, errors, capabilities, and YAML rendering base-owned. Traverse live typed Unreal editor structures rather than reparsing clipboard export text.
- Preserve package dirty state, open editors, selection, transactions, loaded-world state, compile state, and project content.
- Test MCP schema/framing, deterministic YAML and quoting, selectors, paging, snapshots, errors, limits, security, media exclusion, unknown classes, inheritance, every core family, Blueprint Interface declarations, normal/verbose graphs, complete/oversized graphs, removed-tool behavior, and read-only preservation.
- Run the full Python suite, mandatory Windows adaptive/unity and non-unity builds, native Automation, headless and production-socket integration, and base-plugin packaging. Track unavailable macOS verification as preferred follow-up; Linux remains outside support.

### Documentation and completion gate

- Document the tool schema, exact paths, YAML envelope, selectors, paging, graph fallback, supported core families, limits, errors, exclusions, and representative multi-call analysis workflows.
- Complete only when every advertised core family returns deterministic bounded semantic results, graph completeness is explicit, media and live runtime state cannot leak, capabilities match dispatch, read-only preservation passes, and mandatory Windows verification succeeds.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
