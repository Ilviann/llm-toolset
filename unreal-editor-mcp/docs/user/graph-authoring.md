# Blueprint graph authoring

Inspect the target Blueprint and graph first. For mutation identity, reconciliation, compile, and save rules, see [Blueprint mutation](blueprint-mutation.md#reliable-actor-blueprint-mutation).

## Blueprint action catalog

Use inspect first to obtain the current `snapshot_id`, one stable local `graph_id`, and, when needed, exact node/pin IDs. Then ask for the smallest useful action set. For example, discover the getter for one member in an event graph:

```json
{
  "asset_path": "/Game/Actors/BP_Door.BP_Door",
  "graph_id": "0123456789abcdef0123456789abcdef",
  "expected_snapshot": "0123456789abcdef0123456789abcdef01234567",
  "member": "Health",
  "node_family": "variable_get",
  "limit": 5
}
```

Optional filters are exact case-insensitive `text`, exact `owner_class`, `function`, `member`, one `node_family`, and an exact `pin_context` containing `node_id` and `pin_id`. The released families are:

- `function_call`, `variable_get`, and `variable_set` for callable and property actions, including local and inherited members;
- `event` for available inherited event overrides in event graphs, excluding unique events already implemented by the Blueprint;
- `flow_control` for branches, sequences, multi-gates, do-once nodes, switches, enum iteration, and context-valid standard flow-control macros;
- `cast` for object and class dynamic casts, with `owner_class` selecting the target class;
- `literal` for `MakeLiteral*`, enum, bitmask, and self-reference actions; and
- `operator` for common promotable or associative operators.

`function` applies to function-backed `function_call`, `event`, `literal`, and `operator` actions; `member` applies only to variable get/set actions. Every result has already passed Unreal's live Blueprint, graph, uniqueness, and optional pin filters. Function-backed records report pure, static, const, and latent flags; every record reports whether it is a wildcard candidate, and casts report whether they cast class references. Latent calls are excluded from incompatible function graphs, events are excluded from function and macro graphs, and pin context removes incompatible candidates. The catalog never accepts a node class or forged field/spawner signature.

Each result has an opaque `action_id`. Repeating an identical live query reuses IDs, but they expire after 60 seconds and are invalidated by retention eviction, structural snapshot changes, or editor/bridge restart. `blueprint_graph_edit` accepts the ID only for `add_node`; `blueprint_block_replace` accepts context-free IDs for its bounded body plan. The plugin re-resolves rebuildable signatures and reapplies Unreal's live graph filters immediately before use. Re-catalog whenever the ID lifetime or snapshot is uncertain.

Broad scans are intentionally bounded and report `truncated` and `timed_out`. Prefer an exact function/member and family, then add owner class or pin context when ambiguity remains. See [`examples/action-catalog-workflow.json`](../../examples/action-catalog-workflow.json) for inspect-first queries.

## Graph-node lifecycle

Inspect the target graph, catalog one exact action, then create a node using the same graph and snapshot identities:

```json
{
  "operation_id": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
  "asset_path": "/Game/Actors/BP_Door.BP_Door",
  "expected_snapshot": "0123456789abcdef0123456789abcdef01234567",
  "operation": "add_node",
  "graph_id": "0123456789abcdef0123456789abcdef",
  "action_id": "fedcba9876543210fedcba9876543210",
  "position": {"x": 160, "y": 240}
}
```

The result reports whether a node was newly `created` or a unique spawner `returned_existing`, plus the complete changed-node record, stable node/pin IDs, new snapshot, and dirty state. Creation is limited to 2,048 nodes per graph, results encode at most 256 pins per changed node, and each coordinate must be an integer from -1,000,000 through 1,000,000.

Use the returned node ID and latest snapshot to move or remove it:

```json
{
  "operation_id": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
  "asset_path": "/Game/Actors/BP_Door.BP_Door",
  "expected_snapshot": "89abcdef0123456789abcdef0123456789abcdef",
  "operation": "move_node",
  "graph_id": "0123456789abcdef0123456789abcdef",
  "node_id": "11111111111111111111111111111111",
  "position": {"x": 480, "y": -160}
}
```

`remove_node` uses the same shape without `position`. Only local event graphs, editable user-function graphs, and local macro graphs are mutable. Inherited, interface, construction, delegate/signature, intermediate, non-K2, and read-only graphs reject. Required entry/result/tunnel nodes, intermediate nodes, nodes without stable identity, and nodes Unreal does not consider user-deletable cannot be moved or removed. Removal breaks that node's links; reconnect surviving nodes explicitly with `connect_pins` when needed.

Every accepted edit is one Unreal transaction and remains dirty until `blueprint_save`. Re-inspect after each edit, compile, Undo/Redo, save/reload, or bridge restart; never reuse a prior action ID or snapshot. See [`examples/graph-node-lifecycle-workflow.json`](../../examples/graph-node-lifecycle-workflow.json) for the complete three-operation flow.

## Complete function replacement

Inspect the Blueprint's unfiltered `functions` section first, then select the exact function record by ID. The unfiltered result supplies the current structural Blueprint snapshot, while that function record publishes `replacement_boundary` with replaceability, the entry/result IDs, exact old owned-node and local-variable ID sets, and a function fingerprint. Echo that entire boundary plus the snapshot in `blueprint_block_replace`; never infer or omit old identities. A `function_id`-filtered inspection is query-scoped, so do not use its snapshot for the action catalog or replacement precondition.

The first contract replaces one complete editable user-owned function only. Supply body nodes as unique semantic keys plus freshly cataloged context-free action IDs and explicit positions. Pin defaults and connections use those keys and exact pin names; `$entry` and `$result` address the preserved declaration nodes. Conversions require both `automatic_conversion: true` and an explicit conversion position. Events, custom events, macros, overrides, inherited/interface functions, arbitrary regions, and automatic layout remain unsupported.

The plugin applies the plan to an isolated non-transient scratch Blueprint, compiles it, compares its semantic structure, destroys scratch state, rechecks live preconditions, then applies the same plan in one live transaction. Rejected preflight creates no live transaction; an unexpected live mismatch rolls back and verifies the prior snapshot, dirty state, and compile status. A successful result remains dirty: call `blueprint_compile` and `blueprint_save` explicitly.

Body nodes are limited to 64, old owned nodes to 256, locals to 64, defaults to 128, and connections to 256. Use `operation_status` after a lost response; replaying the identical request returns its retained outcome. Prefer `blueprint_graph_edit` for one atomic node, pin, connection, or position change. See [`examples/function-replace-workflow.json`](../../examples/function-replace-workflow.json).

## Complete atomic pin and connection editing

Inspect `pins` after creating nodes and target every pin by its stable node and pin identities. Set one unlinked supported input default with the same tagged K2 values used by members and parameters:

```json
{
  "operation_id": "cccccccccccccccccccccccccccccccc",
  "asset_path": "/Game/Actors/BP_Door.BP_Door",
  "expected_snapshot": "0123456789abcdef0123456789abcdef01234567",
  "operation": "set_pin_default",
  "graph_id": "11111111111111111111111111111111",
  "node_id": "22222222222222222222222222222222",
  "pin_id": "33333333333333333333333333333333",
  "default": {"kind": "literal", "value": 77}
}
```

`engine_default` restores the pin's autogenerated default. Explicit Boolean, numeric, name/string/text/enum, and compatible hard/soft object/class/asset references are parsed by the live K2 schema. Defaults are limited to 512 canonical characters. The pin must be an editable, unlinked supported input; execution, wildcard, linked, orphaned, hidden/read-only/ignored, unstable, and stale pins reject without a transaction. Pin inspection reports the tagged `default` plus bounded raw string/object/text storage for precise read-back.

Create one direct output-to-input connection by supplying both node/pin identity pairs:

```json
{
  "operation_id": "dddddddddddddddddddddddddddddddd",
  "asset_path": "/Game/Actors/BP_Door.BP_Door",
  "expected_snapshot": "89abcdef0123456789abcdef0123456789abcdef",
  "operation": "connect_pins",
  "graph_id": "11111111111111111111111111111111",
  "from_node_id": "44444444444444444444444444444444",
  "from_pin_id": "55555555555555555555555555555555",
  "to_node_id": "22222222222222222222222222222222",
  "to_pin_id": "66666666666666666666666666666666"
}
```

The live schema decides exact compatibility and whether an exclusive input or execution output replaces existing links. Wildcard and numeric-promotion connections specialize through the schema without an extra flag; the result reports `wildcard_specialized` and every reconstructed node/pin identity. Directed cycles reject before a transaction.

Conversion-node insertion is disabled by default. Add `"automatic_conversion": true` only when one specific connection may insert a schema-selected conversion:

```json
{
  "operation_id": "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee",
  "asset_path": "/Game/Actors/BP_Door.BP_Door",
  "expected_snapshot": "0123456789abcdef0123456789abcdef01234567",
  "operation": "connect_pins",
  "graph_id": "11111111111111111111111111111111",
  "from_node_id": "44444444444444444444444444444444",
  "from_pin_id": "55555555555555555555555555555555",
  "to_node_id": "22222222222222222222222222222222",
  "to_pin_id": "66666666666666666666666666666666",
  "automatic_conversion": true
}
```

At most one conversion node may be inserted; its node and all pin IDs are returned in `created_identities` and `changed.nodes`. The connection record reports whether the result is `direct`, whether automatic conversion occurred, the conversion-node count, specialization, and replaced-link count. Each pin is limited to 64 links. `disconnect_pins` uses the same four identities, never accepts conversion opt-in, and requires the exact direct link. After every mutation or lost-response reconciliation, inspect the returned snapshot again before composing the next edit; also re-inspect after compile, Undo/Redo, reload, reconstruction, or bridge restart. See [`examples/complete-atomic-graph-workflow.json`](../../examples/complete-atomic-graph-workflow.json).
