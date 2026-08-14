# Asset inspection

`asset_inspect` reads one exact `/Game` asset. Both `/Game/Actors/BP_Door` and `/Game/Actors/BP_Door.BP_Door` resolve to the canonical object path; discovery, `/Engine`, plugin mounts, filesystem paths, traversal, and media retrieval are unavailable.

Omit `selector` for a compact root containing asset identity, one stable snapshot, semantic family blocks, and exact child selectors. Built-in deep families are Actor and gameplay-framework Blueprints, standalone Actor Component Blueprints, Blueprint Interfaces, Data Asset and Primary Data Asset instances, their Blueprint/Data-Only Blueprint class variants, and Data Tables. Base UMG, Animation Blueprints, MVVM, Materials, and Niagara do not yet receive deep built-in records. When admitted, optional GAS and CommonUI companions add their own bounded blocks and selectors without replacing the base identity.

```json
{"asset_path":"/Game/Actors/BP_Door"}
```

Use selectors returned by the root, for example `event_graphs/EventGraph`, `events/EventGraph/BeginPlay`, `functions/ComputeValue`, `macros/ClampValue`, `components/Trigger`, `properties`, `asset_bundles`, `rows`, `rows/Axe`, `columns/Damage`, `gameplay_ability`, `gameplay_effect`, `commonui_widget`, `commonui_activation`, `commonui_references`, or an exact nested collection selector copied from a value descriptor. Selector segments use uppercase UTF-8 percent encoding.

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

Every success is deterministic safe YAML inside the ordinary MCP text-content result. Structured errors remain JSON-RPC/MCP errors. YAML output preserves JSON Boolean, null, finite number, string, sequence, and mapping types, sorts mapping keys, quotes strings, and never emits aliases or executable tags.

See [`examples/asset-inspection-workflow.json`](../../examples/asset-inspection-workflow.json) for a compact multi-call sequence and the [asset inspection contracts](../types/asset-inspection/index.md) for exact family behavior.
