# Blueprint graph editor

## Ownership

`FUnrealMCPBlueprintGraphEditor` is the mutating `blueprint_graph_edit` facade after operation admission and Game-thread dispatch. It owns exact request decoding, mutable Actor Blueprint/graph/node resolution, protected-target policy, graph/position/pin bounds, one-node transactions, persistent identity completion, postcondition verification, and concise change records. `FUnrealMCPBlueprintActionCatalog` continues to own opaque action identities and freshly re-resolves/re-filters a retained rebuild signature for `add_node`; the graph editor invokes only that resolved live spawner.

## Dependency direction

The bridge constructs the graph editor from the shared inspector and action catalog. The editor depends inward on the shared mutation-scope/snapshot helpers, Asset Registry, public K2 graph/node APIs, `FScopedTransaction`, `FBlueprintEditorUtils`, and the inspector's bounded pin-type encoder. The inspector remains read-only and does not depend on the graph editor. The action catalog stores no trusted spawner pointer: resolution scans the live action database within the existing action limits immediately before the graph editor opens its transaction.

## Invariants

- `add_node`, `move_node`, and `remove_node` are the only operations. Every request carries an operation ID, exact mutable Actor Blueprint path, current snapshot, stable local graph GUID, and operation-specific retained action or node identity.
- Only locally owned K2 event graphs, editable user-function graphs, and local macro graphs are mutable. Inherited, interface, construction, delegate/signature, intermediate, transient, non-K2, and other read-only graphs reject before a transaction.
- Creation requires a live retained action bound to the same bridge, asset/class, graph/schema, snapshot, normalized query, and optional pin context. The rebuild signature is re-resolved and live-filtered; cached UObject pointers are never invocation authority.
- A spawner may return a new node or an existing unique node. New nodes and every pin must have persistent GUIDs. Existing nodes are not repositioned and are reported with `returned_existing: true`.
- Move/remove require one stable node identity in the exact graph. Intermediate, signature-required, and other nodes for which Unreal reports `CanUserDeleteNode() == false` reject as `protected_node`; removal breaks only the target node's links.
- Graphs contain at most 2,048 nodes, changed-node results encode at most 256 pins, and integer coordinates remain within ±1,000,000. The retained-action scan keeps its 20,000-spawner/one-second ceiling.
- Every accepted edit uses one transaction, marks the Blueprint structurally or non-structurally as the node requires, verifies the resulting snapshot and direct postcondition, and explicitly restores unexpected mutation failure. Rejections and injected no-change spawner failures preserve structure, dirty/compile state, and transaction history.

## Verification

`UnrealMCP.Phase11.GraphNodeLifecycle` covers production action resolution, event/function/macro graphs, pure/impure function and variable nodes, unique events, persistent node/pin identities, invalid/expired actions, injected spawner failure, returned-existing nodes, protected deletion, coordinate bounds, stale snapshots/identities, Undo/Redo, compile, and save. The cross-process script reconciles deliberately lost add/move/remove responses, saves, restarts, and verifies the surviving node position/pin identities and removed-node absence through the production Python client.
