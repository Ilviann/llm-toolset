# Asset inspection

`asset_inspect` reads one exact `/Game` asset. Both `/Game/Actors/BP_Door` and `/Game/Actors/BP_Door.BP_Door` resolve to the canonical object path; discovery, `/Engine`, plugin mounts, filesystem paths, traversal, and media retrieval are unavailable.

Omit `selector` for a compact root containing asset identity, one stable snapshot, semantic family blocks, and exact child selectors. Built-in deep families are Actor and gameplay-framework Blueprints, standalone Actor Component Blueprints, Blueprint Interfaces, base UMG Widget Blueprints, regular/template/Animation Layer Interface Animation Blueprints, Data Asset and Primary Data Asset instances, their Blueprint/Data-Only Blueprint class variants, and Data Tables. MVVM, Materials, and Niagara do not yet receive deep built-in records. When admitted, optional GAS and CommonUI companions add their own bounded blocks and selectors without replacing the base identity.

```json
{"asset_path":"/Game/Actors/BP_Door"}
```

Use selectors returned by the root, for example `event_graphs/EventGraph`, `events/EventGraph/BeginPlay`, `functions/ComputeValue`, `macros/ClampValue`, `components/Trigger`, `widget_tree`, `widgets/HealthText`, `named_slots`, `bindings`, `animation_graphs/AnimGraph`, `state_machines/AnimGraph/Locomotion`, `states/AnimGraph/Locomotion/Idle`, `transitions/AnimGraph/Locomotion/Idle_to_Run`, `parent_asset_overrides`, `properties`, `asset_bundles`, `rows`, `rows/Axe`, `columns/Damage`, `gameplay_ability`, `gameplay_effect`, `commonui_widget`, `commonui_activation`, `commonui_references`, or an exact nested collection selector copied from a value descriptor. Selector segments use uppercase UTF-8 percent encoding.

```json
{
  "asset_path": "/Game/Actors/BP_Door",
  "selector": "event_graphs/EventGraph",
  "verbose": true
}
```

Graphs are atomic and complete by default. An oversized graph returns `data_limit_exceeded`; set `allow_partial_graph: true` only when a coherent, explicitly marked slice is acceptable. Graph selectors reject paging. Collections use zero-based `page_index`, default `page_size: 10`, and maximum `page_size: 100`.

Data Asset roots include cumulative safe authored properties with declaring-class provenance. Every array, set, and map is a selector descriptor; Primary Data Assets additionally expose Primary Asset validity and bounded Asset Bundle pages. Instanced object graphs, delegates, transient/editor-only data, referenced-asset traversal, and media or bulk payloads remain unavailable and are reported as explicit limitations when relevant.

Data Table roots include row-struct identity, client-build and import policy, the complete bounded schema, and a sorted row-name page. Use `rows` for complete row pages, `rows/<name>` for one row, the exact returned row-field selector for nested collections, and `columns/<field>` to compare one field across rows. These views reuse the same schema/value codec and query-independent snapshot as `game_data_inspect`, which remains separately published.

Widget Blueprint roots compose the shared Blueprint member/graph contract with base UMG defaults, an effective inherited/local parent-before-child tree, class-specific presentation, panel-slot layout, named slots, legacy property bindings, and Designer event bindings. Exact widget and nested collection selectors are pageable; graphs retain the common atomic graph rules. Widget Animation timelines, CommonUI-specific properties, MVVM records, Slate/runtime instances, and viewport state are excluded from the base UMG view.

Animation Blueprint roots compose common Blueprint members and K2 declarations with animation mode/settings, target skeleton, linked-layer policy, sync groups, parent overrides, pose/layer graph summaries, and state-machine indexes. Select pose graphs, state/conduit graphs, transition rules, and custom transition blends using the exact returned selectors; these use the common atomic graph contract with output-oriented partial traversal. Animation Layer Interfaces expose layer signatures without implementation node bodies. Animation clips, frames, samples, curves, meshes, thumbnails, live components, active states, montage playback, weights, proxies, debug poses, compiler layout, and referenced-asset traversal are excluded.

Every success is deterministic safe YAML inside the ordinary MCP text-content result. Structured errors remain JSON-RPC/MCP errors. YAML output preserves JSON Boolean, null, finite number, string, sequence, and mapping types, sorts mapping keys, quotes strings, and never emits aliases or executable tags.

See [`examples/asset-inspection-workflow.json`](../../examples/asset-inspection-workflow.json) for a compact multi-call sequence and the [asset inspection contracts](../types/asset-inspection/index.md) for exact family behavior.
