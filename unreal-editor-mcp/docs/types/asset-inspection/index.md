# Asset inspection contracts

This component contract defines the accepted design for bounded semantic inspection of Unreal assets through `asset_inspect`. It is delivered through the linked staged feature set; executable schemas, capabilities, native handlers, and tests become authoritative as each stage is implemented and completed.

## Owning features

- [`asset-inspect-core`](../../features/completed/asset-inspect-core.md) owns the tool, shared contracts, neutral classification, gameplay-framework Blueprints, Actor Components, and Blueprint Interfaces.
- [`asset-inspect-data`](../../features/completed/asset-inspect-data.md) adds Data Assets and Data Tables.
- [`asset-inspect-umg`](../../features/completed/asset-inspect-umg.md) adds base UMG Widget Blueprint inspection.
- [`commonui-assets-inspect`](../../features/completed/commonui-assets-inspect.md) adds the released CommonUI root-widget overlay.
- [`gas-ability-blueprints-inspect`](../../features/completed/gas-ability-blueprints-inspect.md) and [`gas-gameplay-effects-inspect`](../../features/completed/gas-gameplay-effects-inspect.md) add the released GAS overlays.
- [`asset-inspect-animation`](../../features/completed/asset-inspect-animation.md) adds Animation Blueprint inspection.
- [`commonui-umg-types-inspect`](../../features/completed/commonui-umg-types-inspect.md) expands CommonUI coverage to widgets and values throughout supported UMG assets.
- [`umg-mvvm-inspect`](../../features/planned/umg-mvvm-inspect.md) adds ViewModel Blueprint and Widget MVVM inspection.
- [`ai-assets-inspect`](../../features/completed/ai-assets-inspect.md) adds Behavior Tree, Blackboard, EQS, and custom AI Blueprint families.
- [`enhanced-input-assets-inspect`](../../features/completed/enhanced-input-assets-inspect.md) adds released Enhanced Input asset and nested trigger/modifier families.
- [`gas-supporting-assets-inspect`](../../features/completed/gas-supporting-assets-inspect.md) expands GAS coverage to supporting Blueprint asset families.

Feature front matter and the roadmap are authoritative for direct prerequisites and implementation order.

## Asset-family contracts

- [`asset-types/`](asset-types/index.md) — accepted staged property and selector contracts by asset family.

## Contract decisions

- Add an easy Unreal MCP inspection feature with a relatively small model-facing API footprint.
- Its primary purpose is to let an LLM analyze logic, algorithms, and data that are otherwise enclosed in binary Unreal `.uasset` files.
- Extract as much semantically useful information as is safely available for the selected asset rather than limiting inspection to a small common metadata summary.
- The single general model-facing tool is named `asset_inspect`.
- Every non-error `asset_inspect` response is deterministic YAML text. JSON remains the MCP JSON-RPC transport and native bridge representation, not the successful model-facing payload format for this tool.
- Every successful YAML response contains the asset's `type`.
- Include a `parent_type` only when inheritance is semantically meaningful, such as for a custom Actor Blueprint. Omit the field entirely when it is meaningless; do not return it as `null` or expose the superclass of a standard `Texture2D` merely because one exists.
- Never return media content. Initial support for media assets returns type classification only; useful bounded metadata may be added later in response to usage feedback.
- Treat raw content-bearing assets as media for the initial type-only policy, including textures, raw audio or video, mesh geometry, animation clips, thumbnails, and similar bulk content. Do not classify an asset as media merely because it belongs to an artistic system: logic-bearing Materials, Niagara Systems or Emitters, Animation Blueprints, Sound Cues, Control Rigs, and similar graph or configuration assets require deep semantic inspection.
- Require one exact project-content asset path as an `asset_inspect` parameter. The tool does not discover or search by folder, name, class, or family.
- Accept either exact Unreal package form such as `/Game/Folder/Asset` or object form such as `/Game/Folder/Asset.Asset`, and normalize both to the canonical object path before loading, inspection, response generation, snapshot calculation, and page selection.
- Reuse safe codecs and structural fingerprinting where their semantics match. `game_data_inspect` remains published; the reconstruction-oriented Blueprint facade was removed when `asset_inspect` covered its core analysis role.
- Base UMG Widget Blueprint logic, hierarchy, layout, presentation, named slots, and bindings are available through `asset-inspect-umg`. CommonUI companion records compose separately through API-v2 adapters; MVVM remains separate.
- Exclude Widget Animation timeline inspection from `asset-inspect-umg`. Animation variables and calls may still appear as ordinary references or call nodes in Widget Blueprint logic graphs, but the tool does not inspect animation bindings, MovieScene tracks, sections, or keyframes.
- Keep core inspection independent of GAS. When the optional API-v2 GAS companion is admitted, its Gameplay Ability and Gameplay Effect overlays compose without making the companion a base requirement.
- Optimize output for semantic analysis of the asset's behavior, algorithms, structure, relationships, and meaningful data. One-to-one asset reconstruction from the result is explicitly out of scope.
- Use hierarchical inspection rather than automatically emitting every semantic detail. A default request returns a compact asset-specific index; a targeted request returns the selected logical child in depth.
- When an important non-graph property group can become large, keep its small semantic summary and child count in the root response and advertise an exact selector for bounded, pageable detail. Do not omit the property group merely to reduce the root footprint. Graphs are an explicit atomic exception and are never paged.
- Across every asset family, every reflected or semantic array, set, or map property is selector-addressable and pageable. The containing response shows its kind, element/key/value type where useful, total count, and exact selector instead of expanding an unbounded value inline. This applies to base framework properties, derived properties, Blueprint-variable defaults, DataAsset fields, companion contributions, and nested collection fields.
- For an Actor-derived Blueprint, the default response contains `type`, meaningful `parent_type`, locally defined custom variables with type and default value, event graphs with their events, functions, and macros.
- A targeted Blueprint graph, function, or macro request contains the asset `type`, selected logical graph name, and all semantic nodes and links in that graph. It omits editor coordinates, Unreal node and pin GUIDs, and other presentation or mutation-only details.
- Every graph-like selector is atomic, including event graphs, event slices, functions, macros, animation pose graphs, state-machine topology, state or conduit graphs, transition rules, custom transition blends, and future Material or Niagara graphs. By default, a successful response contains the complete selected graph. Graphs are never paged and the model is never asked to assemble one from pages. The explicit oversized-graph fallback below may return one clearly marked coherent partial slice only when the caller opts in.
- Use one optional hierarchical `selector` string for targeted inspection across every asset family rather than a Blueprint-specific `graph` parameter. Omitting it requests the asset-family root summary.
- Add optional integer `page_size` and `page_index` parameters to the common tool shape. `page_size` defaults to a relatively small 10 records and is bounded from 1 through the existing maximum of 100. `page_index` is zero-based and selects a deterministic page of a pageable non-graph collection. Supplying either paging parameter for a graph-like selector returns `invalid_argument` rather than silently ignoring it.
- Add optional Boolean `allow_partial_graph` with default `false`. It applies only to graph-like selectors and is not a paging control. Supplying it for a non-graph selector returns `invalid_argument`. If the selected graph exceeds the hard complete-response ceiling, `false` returns `data_limit_exceeded`; `true` returns one deterministic coherent partial slice when at least a useful slice can be represented.
- All numeric index fields exposed or accepted anywhere in the `asset_inspect` contract are zero-based. Counts and priorities are not indexes.
- Response-local node IDs must be short and contain a compact hint identifying the node's semantic type, such as `branch1`, `call2`, or `get3`.
- An event selector returns the nodes reachable from that event through execution flow, recursively includes pure and data-producing nodes required by those reachable nodes, and excludes disconnected events. Function and macro invocations remain compact call nodes whose bodies are available through separate selectors.
- Add an optional Boolean `verbose` parameter with default `false`. When `true`, selected graph output keeps the semantic representation and additionally exposes Unreal node and pin GUIDs plus node coordinates for debugging and locating nodes in the Blueprint Editor.
- Deduplicate callable signatures. Asset-owned function and macro signatures appear once in the root index and once as the header of their selected body; each selected graph defines every unique referenced callable once in a graph-local `callables` table. Call nodes contain only a callable reference, call-site target information, and literal argument overrides.
- A selected function graph header lists all function-local variables with name, K2 type, and effective default value when available.
- Emit every graph edge exactly once under its source node's output-pin `links` map. Do not return a separate graph-level link list; each output pin maps to a list of compact destination node/input-pin endpoints.
- Keep the neutral base `type` and meaningful `parent_type` response available for companion-owned asset types even when their companion is absent; omit domain-specific sections when unavailable.
- Future companion plugins can contribute additional bounded root properties and hierarchical selector data blocks for one exact target asset class and its derived classes without publishing another model-facing tool.
- Every response with deeper inspectable children contains a compact response-level `selectors` list naming the supported child namespaces. The adjacent semantic collections provide the graph, event, function, macro, row, widget, or companion-block names used as subsequent selector path segments.
- Inspection of a supported derived asset is cumulative across its Unreal framework inheritance chain: return all important semantic properties from supported base framework types and add the important properties introduced by each derived type. Emit each property once, grouped by or annotated with its declaring type, rather than presenting only the most-derived class delta.

## Delivery stages

### Delivery order

1. `asset-inspect-core`: common API and contracts, neutral/media classification, Actor Blueprints, Actor-owned components, standalone Actor Component Blueprints, GameInstance, GameMode, GameState, PlayerController, PlayerState, and Blueprint Interfaces.
2. `asset-inspect-data`: Data Assets, Primary Data Assets, Blueprint/Data-Only Blueprint class variants, and Data Tables.
3. `asset-inspect-umg`: base UMG Widget Blueprint logic, hierarchy, layout and bindings without Widget Animations, CommonUI, or MVVM-specific sections.
4. `asset-inspect-animation`: regular, template, and Animation Layer Interface Animation Blueprints, including pose graphs and state machines.

### Later stages

Add or expand `asset_inspect` coverage through the linked planned features for:

- [Animation Blueprints](asset-types/animation-blueprint.md)
- [CommonUI types throughout supported UMG assets](../../features/completed/commonui-umg-types-inspect.md)
- [MVVM ViewModels and Widget bindings](../../features/planned/umg-mvvm-inspect.md)
- [Behavior Trees, Blackboards, EQS, and custom AI Blueprints](../ai-asset-inspection/index.md)
- [Enhanced Input assets and nested types](../enhanced-input-asset-inspection/index.md)
- [Supporting GAS Cue Notify, Attribute Set, and calculation Blueprints](../../features/completed/gas-supporting-assets-inspect.md)

Materials, Material Functions, Niagara Systems, and Niagara Emitters remain uncommitted candidates without roadmap features.

### Current implementation fit

- The released Blueprint family policy already deeply inspects Actor, GameMode/GameModeBase, GameState/GameStateBase, and GameInstance Blueprints. PlayerController and PlayerState descendants currently travel through the general Actor family.
- Actor-owned components are already records within supported Actor-family Blueprint inspection. `asset-inspect-core` also includes standalone Actor Component Blueprint assets, which are not a published Blueprint family and therefore need a new classification and collector path.
- Blueprint Interfaces are declarations-only core families. Data Asset and Primary Data Asset instances plus their Blueprint class variants are deeply inspected; Animation Blueprints compose a dedicated built-in semantic overlay.
- Data Tables reuse the separate game-data schema/value collectors and snapshots behind `asset_inspect`; `game_data_inspect` remains published for its cursor-oriented contract.
- Ordinary Widget Blueprint logic and tree inspection is implemented by the base `asset-inspect-umg` overlay. Released CommonUI inspection covers `UCommonUserWidget`-derived roots; broader CommonUI tree/value inspection and MVVM inspection remain separate planned features.
- GAS Ability, GAS Effect, Cue Notify, Attribute Set, and calculation collectors compose through optional inspection overlays. Their absence or rejection changes no base family, schema, or response contract.
- AI inspection remains planned as an independent companion contribution. Released Enhanced Input inspection uses the same optional boundary, so the base plugin retains no direct dependency on either system.

## Current repository evidence

- The published catalog uses the approved exact `asset_inspect` name; no legacy `inspect-asset` or `inspect_asset` alias is accepted.
- Public tools use lowercase snake case and generally follow `<domain>_<operation>`, including `asset_inspect`, `level_inspect`, and `game_data_inspect`.
- Nearby asset capabilities remain split across exact semantic inspection, inbound references, game-data-specific reads, and level inspection.
- The internal reconstruction-oriented Blueprint collector still supplies authoring fingerprints, but `asset_inspect` owns the model-facing semantic hierarchy and direct `UEdGraph` traversal.
- The MCP transport and native bridge use JSON. Python renders successful `asset_inspect` results as deterministic safe YAML inside one MCP text-content item; structured errors retain the shared JSON-shaped contract.
- The Python package has no runtime dependencies. Its dedicated renderer accepts only JSON-compatible values and emits the documented safe YAML subset.
- The existing Blueprint inspector already traverses live `UEdGraph`, node, pin, and connection objects directly. It does not depend on `FEdGraphUtilities::ExportNodesToText`, although its current mutation-oriented records expose GUIDs and coordinates that the new semantic view should hide.

## Naming rationale

The accepted `asset_inspect` name:

- It matches the existing snake-case MCP catalog.
- It follows the established noun-then-operation order.
- It leaves `asset_*` tools adjacent and searchable in model context.

## Proposals

### Companion-independent type identity

The base plugin should always own the neutral `type` and meaningful `parent_type` envelope, including for assets whose deeper semantics belong to a companion. This does not require a compile-time dependency on the companion's domain plugin.

For a Blueprint, obtain the generated or represented class path and immediate parent class path through loaded `UBlueprint` and `UClass` metadata when available. Asset Registry Blueprint tags provide a bounded fallback for class-path identity without requiring domain headers. For a Gameplay Ability Blueprint, a base-only result can therefore be:

```yaml
asset:
  type: /Game/Abilities/GA_Fireball.GA_Fireball_C
  parent_type: /Script/GameplayAbilities.GameplayAbility
```

The base treats `/Script/GameplayAbilities.GameplayAbility` as an opaque exact Unreal class identity. It does not include GAS headers, add `GameplayAbilities` to `UnrealMCP.Build.cs`, hard-code GAS property names, cast to `UGameplayAbility`, or claim GAS-specific semantic support. A future GAS companion may augment the same response with Gameplay Ability policies, tags, triggers, effects, and other typed semantics after capability and API admission.

If the domain plugin or parent class cannot be resolved, the base may return registry-derived type identity with an explicit unresolved or partial classification marker, but must not fabricate ancestry or inspect domain properties through unrestricted reflection. Absence of a companion should not prevent a successful type and parent response when authoritative Blueprint metadata is available.

This neutral envelope does not change the companion API. Any later companion contribution to `asset_inspect` remains a separate API-impact decision subject to the repository's companion-version workflow.

### Companion-contributed properties and selectors

The base owns target resolution, the neutral asset envelope, selector parsing and routing, snapshots, bounds, merge policy, and final YAML rendering. A companion registers one exact target class path with optional derived-class matching, one collision-free output-block name, and one or more selector namespaces. It returns typed JSON-compatible records to the base; it never emits YAML directly.

Companion root properties are appended inside the registered block and cannot replace base `asset`, type, parent, paging, `selectors`, snapshot, or error fields. Selector prefixes are namespaced, such as `gameplay_ability/policies`, so multiple applicable companions and base families cannot claim the same logical path. If contributions for a base class and a more-derived class both apply, they may compose only through distinct registered blocks or selector prefixes.

Example future root response:

```yaml
asset:
  type: /Game/Abilities/GA_Fireball.GA_Fireball_C
  parent_type: /Script/GameplayAbilities.GameplayAbility
selectors: [gameplay_ability]
gameplay_ability:
  instancing_policy: instanced_per_actor
```

Selecting `gameplay_ability` can then return its own properties plus `selectors: [policies, tags, triggers]`; the caller addresses a child as `gameplay_ability/policies`, `gameplay_ability/tags`, or `gameplay_ability/triggers`. Each selector list is relative to the response's current selector path.

The same discoverability rule applies to base content. Functions, macros, graphs, events, Data Table rows, Widget subtrees, and future contributed blocks are represented by semantic collections plus the compact response-level selector namespaces that address those collections. Callers should copy the exact returned selector rather than synthesize one from a display name.

Selector paths use `/` between segments. Encode every name-derived segment as UTF-8 and percent-encode each byte outside the ASCII unreserved set `A-Z`, `a-z`, `0-9`, `.`, `_`, `~`, and `-`; `/`, `%`, control bytes, whitespace, and non-ASCII bytes are therefore encoded with uppercase hexadecimal digits. Routing decodes each segment exactly once after splitting, rejects malformed encodings and decoded null/control characters, and compares the decoded value with the exact Unreal semantic name. Namespace segments owned by the schema remain literal unreserved ASCII.

Only admitted live contributions appear in root output or selector routing. Each contribution declares stable limits and fingerprint material; companion records participate in the same asset snapshot and pagination bounds. Missing, disabled, incompatible, or unready companions leave the neutral base response intact and expose none of their blocks or selectors.

Companion API v2 asset families declare exact class policy, bounded root blocks, selector routes, and snapshot builders. Admitted inspection-only families compose over the base storage family in deterministic family-ID order. Block, selector, or snapshot collisions fail closed; absence or rejection leaves the neutral base response intact. GAS and CommonUI use this contract without changing the API-v2 or schema-revision values.

### Capability shape

Use one read-only facade with a small common request shape and typed, paginated result records. The facade should classify the target asset, report its supported inspection sections, and delegate deeper semantic extraction to bounded asset-family inspectors.

Keep the target asset path required on every page request. Pages are selected explicitly with `page_size` and `page_index`; opaque continuation cursors are not part of the `asset_inspect` API. Every page returns the same query-independent asset `snapshot_id` while the asset is unchanged. A caller combining pages must compare those IDs and restart if the asset changed between calls.

### Collection property selectors

Known semantic adapters keep scalar and bounded non-collection struct properties in their owning framework block. Every array, set, or map value is represented by a collection descriptor:

```yaml
component_tags:
  kind: array
  item_type: name
  count: 37
  selector: properties/actor_component/component_tags
```

Calling the returned selector with zero-based `page_index` produces a page of values. Arrays preserve native order and expose each item's stable-within-snapshot zero-based `index`. Sets use deterministic type-aware value order. Maps use deterministic type-aware key order and expose zero-based entry indexes plus separate `key` and `value`. Set/map indexes are presentation locations within one snapshot, not persistent asset identities.

Collection values nested inside structs or collection elements receive exact child selectors returned beside the nested descriptor. Array children use `items/<index>` path segments; set and map children use their deterministic snapshot-local entry indexes. Callers should copy returned selector strings rather than synthesize them. Strings, names, text, byte blobs, and structs are not treated as collections merely because they have internal storage.

The default page size is 10 and the maximum is 100. Pages report size, index, page count, returned count, total item count, previous/next availability, and snapshot. Object/class elements remain exact references and are not recursively inspected. Unsupported recursive, instanced, bulk, or media-bearing values return explicit limitations rather than raw serialization.

Graph nodes, pins, links, state topology, and other parts of a selected graph are not collection-property pages. They follow the complete-graph and explicitly opted-in oversized partial-graph rules.

The universal path should attempt safe identity, class/family classification, package metadata, dependencies or references, reflected semantic properties, and explicit support/limitation records. Family-specific extractors should add all safely available logic, relationships, algorithms, and structured values because `.uasset` is a package format used by arbitrary engine, project, and third-party UObject classes rather than one universal semantic schema.

“As much as possible” should mean comprehensive semantic coverage through bounded pages for non-graph collections, targeted sections, explicit depth and collection limits, and visible truncation records. Graph selectors instead return one complete normalized graph so the model can judge whether it should be refactored or decomposed. It should not mean recursive traversal of unrelated objects without a limit.

Project-wide output, memory, and execution safety still require a hard last-resort ceiling. If one complete graph cannot be represented within that ceiling and `allow_partial_graph` is false, return a stable `data_limit_exceeded` error with measured node/link counts and the applicable limit. When the caller explicitly enables the fallback, return a successful YAML partial-graph record as specified below. Size the complete-graph ceiling for unusually large real graphs rather than ordinary page limits.

Prefer records that explain control flow, data flow, ordering, conditions, dependencies, ownership, inheritance, configuration, and cross-record relationships. Omit serialization-only details when they do not improve semantic analysis, and do not preserve binary layout or every value solely to enable round-trip reconstruction.

Media payloads are outside this goal even when technically readable. Do not emit texture pixels, audio or video content, mesh geometry buffers, thumbnails, source-art bytes, encoded media, or filesystem paths intended to retrieve those payloads.

Internal specialized collectors remain authoritative where authoring preconditions or family codecs depend on them. `asset_inspect` owns the model-facing semantic contract; `game_data_inspect` remains published, while the former reconstruction-oriented Blueprint tool was removed in 0.36.0. Admitted API-v2 companion inspection families contribute only through the common facade.

### Output implementation

Implement the confirmed deterministic YAML representation at the final MCP text boundary because it can reduce structural punctuation and make large nested inspection results easier to scan. Do not change MCP's JSON-RPC transport or the authenticated native bridge: native collectors should continue returning typed JSON values, with Python rendering every successful `asset_inspect` response into a safe YAML 1.2-compatible subset.

The renderer should remain dependency-free, quote ambiguous strings, forbid YAML tags, anchors, aliases, and custom types, preserve explicit type discriminators, and produce one self-contained response with format and schema-version fields. Canonical internal records and tests should remain JSON-shaped so YAML presentation cannot alter asset semantics.

Error responses should retain the shared `{code,message,details,retryable}` MCP tool-error contract and `isError` marker; the YAML-only requirement applies to non-error responses.

Both YAML and JSON are widely understood and results vary by model. Validate the confirmed presentation choice against representative asset fixtures for readability, token cost, deterministic rendering, and lossless correspondence with the canonical typed records.

### Type classification

Use stable Unreal class identities rather than localized display names. The schema must distinguish an asset object's storage class from a represented/generated class when an asset defines a type, especially for Blueprints. `parent_type` should describe only a meaningful represented-type inheritance relationship and should not imply that ordinary media-class inheritance is useful to the caller.

For the initial release, raw media-family records stop after this classification envelope. A later feature may add bounded analysis-oriented metadata, such as whether a Static Mesh has collision and how many LOD levels it contains, without adding media payloads. Logic-bearing assets in media-adjacent systems are not subject to this type-only restriction.

### Hierarchical semantic views

For Actor-derived Blueprints, the root view is a semantic table of contents rather than a full graph dump. Its YAML shape is conceptually:

```yaml
asset:
  type: actor_blueprint
  parent_type: /Script/Engine.Actor
variables:
  - name: Health
    type: float
    value: 100.0
event_graphs:
  - name: EventGraph
    events: [BeginPlay, AnyDamage]
functions: [CalculateDamage]
macros: [ClampHealth]
selectors: [event_graphs, events, functions, macros]
```

A selected logic-unit view returns a semantic graph. It should include graph kind and name, nodes described by operation, callable or property identity, meaningful pin names, types and defaults, and explicit execution and data links. It should remove coordinates, comments that do not affect meaning, editor display state, GUIDs, and other clipboard or authoring noise.

Links cannot be represented unambiguously without identifying repeated nodes. Assign compact deterministic response-local labels such as `branch1` or `call2`; each label contains a short semantic-type prefix. These are presentation references, not Unreal GUIDs or mutation identities. Preserve native GUIDs internally for exact resolution, snapshotting, and deterministic tie-breaking when required, but do not expose them in YAML.

The request shape has required `asset_path` and optional `selector`, `verbose`, `page_size`, `page_index`, and `allow_partial_graph`. Examples include `functions/CalculateDamage`, `macros/ClampHealth`, `event_graphs/EventGraph`, `rows/Sword`, and `widgets/InventoryPanel`. The selector namespace disambiguates logical-unit kinds that may share a display name. Paging parameters apply only to pageable non-graph collections; `allow_partial_graph` applies only to graph-like selectors.

A `functions/<name>` response begins with one function header containing the deduplicated input/output signature, semantic traits, and a `local_variables` collection. Each local-variable entry contains its name, K2 type, and effective default value when available; the graph body then refers to that local by name through normalized get/set nodes. Do not repeat local declarations on each node.

The optional `verbose` flag defaults to `false` and is part of the exact query state. With `false`, graph nodes and links contain only semantic fields and compact response-local IDs. With `true`, each node additionally contains its native node GUID and editor `x`/`y` coordinates; pins expose native pin GUIDs where Unreal supplies them. Unreal links do not have independent GUIDs, so verbose links identify their exact endpoints through native node and pin GUID pairs. Verbose mode does not enable media output, recursive reflection, mutation data unrelated to graph location, or larger safety limits. Consequently, an opted-in partial verbose response may contain fewer semantic nodes than a non-verbose response under the same hard byte ceiling.

### Oversized graph fallback

`allow_partial_graph` is a last-resort analysis fallback, not ordinary pagination. If the normalized selected graph fits the complete-response ceiling, the tool returns it completely even when the flag is true. If it does not fit, the successful YAML response must make incompleteness impossible to miss:

```yaml
graph_status:
  complete: false
  reason: graph_limit_exceeded
  selection_strategy: execution_from_entry
  total_nodes: 1840
  total_links: 2960
  detailed_nodes: 220
  boundary_stub_nodes: 34
  returned_links: 410
  omitted_nodes: 1586
  omitted_links: 2550
  limit_bytes: 4194304
```

The server chooses the largest coherent slice that safely fits; `page_size` and `page_index` never control it. Selection is deterministic for the same asset snapshot, selector, `verbose` value, limits, and plugin version:

- Execution graphs start at selected entry points in stable semantic order, traverse execution flow in deterministic breadth-first order, and include the pure/data producers required by retained executable nodes.
- Pose and other output-oriented data-flow graphs traverse backward from result/output nodes so the returned slice explains how the final result is produced.
- State-machine topology starts at the entry state and expands transitions outward, retaining transition endpoints together.
- More specific selectors, such as one event, function, state, or transition rule, remain the preferred way to reduce scope before enabling partial output.

Never cut a semantic node record in half. Directly connected omitted neighbors appear as lightweight `omitted_body: true` boundary-stub nodes with semantic type/name when available. Boundary-stub outgoing links stay inline under the stub, preserving the rule that each represented edge belongs to its source node. The response includes only callable definitions referenced by retained detailed or boundary nodes.

A partial response is one stable first-look slice for proposing decomposition steps; repeated calls with page indexes do not reveal subsequent chunks. If even a minimally coherent slice and its status cannot fit, return `data_limit_exceeded`. Complete graph responses include `graph_status.complete: true` so callers can distinguish them mechanically from the opted-in fallback.

### Event slices and graph source

An event selector such as `events/EventGraph/BeginPlay` produces a semantic slice rather than relabeling the whole Event Graph. Start from the selected event entry, follow reachable execution links, recursively include the pure and data nodes that feed included nodes, and emit only links whose endpoints are in the slice. Do not inline called function or macro bodies; keep each invocation as a semantic call node so the caller can inspect its dedicated selector separately.

Read live `UEdGraph` structures and reuse the existing typed collectors. Do not generate and reparse `FEdGraphUtilities::ExportNodesToText` as the authoritative extraction path: that clipboard-oriented format contains positions, GUIDs, object-export syntax, and engine-version details that would then need to be discarded. Exported text may be used only as a development comparison fixture if it reveals semantic fields missing from direct traversal.

### Call-function node contract

A normalized call-function node should not repeat a complete function signature at every call site. A selected graph instead contains one `callables` table with one entry per unique referenced function. Each entry contains canonical function name, exact defining `owner_type`, `dispatch`, semantic `traits`, input and output types, and declared parameter defaults. Asset-owned function and macro signatures also appear once in the asset root index and once as the header of their own selected body.

Each call node then contains only:

- Short response-local `id`, such as `call1`, and normalized `kind: call_function`.
- One short `callable` reference into the graph-local table.
- `target: self` when an instance target is implicit; an explicit target remains an ordinary data link.
- Literal argument overrides that differ from the callable's declared defaults. Linked arguments are represented only by links.
- Explicit unsupported or unresolved function state when no safe callable definition can be produced.

Attach outgoing links to their source node instead of maintaining a separate graph-level edge list. A node's `links` map uses each semantic output-pin name as a key and a destination list as its value. Each destination identifies one compact `node.input_pin` endpoint. This stores every edge once, keeps branch labels such as `true`, `false`, `completed`, and `then` beside their source operation, and naturally represents output fan-out.

Example non-verbose YAML:

```yaml
callables:
  fn1:
    name: ApplyDamage
    owner_type: /Script/Engine.GameplayStatics
    dispatch: static
    inputs:
      DamagedActor: {type: object</Script/Engine.Actor>}
      BaseDamage: {type: float, default: 0.0}
      EventInstigator: {type: object</Script/Engine.Controller>, default: null}
      DamageCauser: {type: object</Script/Engine.Actor>, default: null}
      DamageTypeClass: {type: class</Script/Engine.DamageType>, default: /Script/Engine.DamageType}
    outputs:
      ReturnValue: {type: float}
nodes:
  - id: branch1
    kind: branch
    links:
      true: [{to: call1.execute}]
      false: [{to: call2.execute}]
  - id: get1
    kind: get_variable
    variable: DamagedActor
    links:
      value: [{to: call1.DamagedActor}]
  - id: call1
    kind: call_function
    callable: fn1
    arguments: {BaseDamage: 25.0}
    links:
      then: [{to: set1.execute}]
      ReturnValue: [{to: set1.value}]
  - id: call2
    kind: call_function
    callable: fn1
    arguments: {BaseDamage: 50.0}
```

With `verbose: true`, keep this semantic shape and add a nested `debug` block containing the native node GUID, `x` and `y` position, and a pin-name-to-native-GUID map. Each inline link destination additionally carries native from and to node and pin GUIDs. Do not replace the readable local endpoint with GUIDs.

Omit the editor title when it only repeats the canonical function, cosmetic pin or display metadata, tooltip text, node dimensions, enabled-state UI, advanced-pin expansion, and hidden auto-wired pins. If a hidden or advanced pin changes runtime meaning, summarize that meaning through the callable definition, `target`, arguments, or links rather than silently discarding it.

## Design status

Requirements gathering is complete. Implementation discoveries that materially change this contract require an explicit design update rather than an undocumented divergence.

## Rejected alternatives

- Treating `asset_inspect` as a lossless exporter or requiring enough output to reconstruct an asset one-to-one. That would enlarge the schema and output, conflict with the no-media rule, and add data that does not serve the semantic-analysis goal.

## Verification evidence

- The Python catalog publishes `asset_inspect` beside `asset_references`, `level_inspect`, and `game_data_inspect`; it publishes no legacy asset-inspection alias and no `blueprint_inspect` tool.
- Successful asset responses are rendered through the dependency-free deterministic safe-YAML boundary; native bridge records and structured errors remain JSON-shaped.
- Native and cross-process coverage verifies canonical paths, selectors, paging, snapshots, family classification, graph bounds, and removal of the former public Blueprint route.
