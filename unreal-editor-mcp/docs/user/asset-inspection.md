# Asset inspection

`asset_inspect` reads one exact `/Game` asset. Both `/Game/Actors/BP_Door` and `/Game/Actors/BP_Door.BP_Door` resolve to the canonical object path; discovery, `/Engine`, plugin mounts, filesystem paths, traversal, and media retrieval are unavailable.

Omit `selector` for a compact root containing asset identity, one stable snapshot, semantic family blocks, and exact child selectors. Core deep families are Actor and gameplay-framework Blueprints, standalone Actor Component Blueprints, and Blueprint Interfaces. Data Assets/Tables, UMG, Animation Blueprints, GAS, CommonUI, MVVM, Materials, and Niagara do not receive deep core records.

```json
{"asset_path":"/Game/Actors/BP_Door"}
```

Use selectors returned by the root, for example `event_graphs/EventGraph`, `events/EventGraph/BeginPlay`, `functions/ComputeValue`, `macros/ClampValue`, `components/Trigger`, or a pageable collection selector. Selector segments use uppercase UTF-8 percent encoding.

```json
{
  "asset_path": "/Game/Actors/BP_Door",
  "selector": "event_graphs/EventGraph",
  "verbose": true
}
```

Graphs are atomic and complete by default. An oversized graph returns `data_limit_exceeded`; set `allow_partial_graph: true` only when a coherent, explicitly marked slice is acceptable. Graph selectors reject paging. Collections use zero-based `page_index`, default `page_size: 10`, and maximum `page_size: 100`.

Every success is deterministic safe YAML inside the ordinary MCP text-content result. Structured errors remain JSON-RPC/MCP errors. YAML output preserves JSON Boolean, null, finite number, string, sequence, and mapping types, sorts mapping keys, quotes strings, and never emits aliases or executable tags.

See [`examples/asset-inspection-workflow.json`](../../examples/asset-inspection-workflow.json) for a compact multi-call sequence and the [asset inspection contracts](../types/asset-inspection/index.md) for exact family behavior.
