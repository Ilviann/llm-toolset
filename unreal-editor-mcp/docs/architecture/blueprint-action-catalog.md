# Blueprint action catalog

## Ownership

`FUnrealMCPBlueprintActionCatalog` is the read-only `blueprint_action_catalog` facade after Game-thread dispatch. A typed query decoder owns exact filter normalization, a focused scanner owns target-first database traversal plus family classification/record encoding, and the facade owns live Blueprint/graph/pin resolution plus retained catalog/action identity. The scanner releases function-call, variable get/set, event, flow-control, cast, literal, and operator families through one classifier.

## Dependency direction

The editor bridge constructs the catalog facade with the bridge-instance identity and shared inspector. The inspector supplies the authoritative structural snapshot. Query decoding has no live-object dependency; live resolution precedes scanning; scanning depends on public Blueprint action database/filter/spawner APIs and resolved K2 context; retained-cache management consumes encoded candidate records. The catalog does not depend on the mutator, transactions, compilation, saving, selection, or editor UI.

## Invariants

- Every query requires one exact Actor Blueprint asset path, stable graph GUID, and current structural snapshot.
- Optional filters are exact case-insensitive text, owner-class path, function name, member name, node family, and one exact node/pin context. Function and variable filters cannot be combined incompatibly.
- Only `function_call`, `variable_get`, `variable_set`, `event`, `flow_control`, `cast`, `literal`, and `operator` records are released. Callers never supply a node class, field signature, or spawner and cannot invoke an action in Phase 10.
- Event spawners are accepted only for real event functions and are suppressed when their unique event already exists. Flow control covers direct supported K2 flow nodes and context-valid standard flow-control macro actions. Literal and operator classification precedes generic function-call classification.
- Unreal's live action filter remains authoritative for graph kind, Blueprint family, latent availability, static/instance context, inheritance, and optional pin compatibility. Promotable operators remain observable as wildcard candidates without specializing them.
- Every action passes Unreal's live Blueprint/graph/pin action filter. Results expose descriptive metadata, not unrestricted construction data.
- Opaque action IDs are process-local, bound through the retained query to bridge instance, target generated class, graph schema and GUID, structural snapshot, normalized filters, pin context, and an internal rebuildable action signature.
- Identical live queries reuse retained IDs. Records expire after 60 seconds; expiry, capacity eviction, snapshot changes, and bridge restart invalidate them.
- A query returns at most 50 results, scans at most 20,000 database spawners for at most one second, retains at most 32 catalogs and 256 actions, and permits one Game-thread catalog operation at a time. Global request/response and queue limits still apply.
- Cataloging preserves package dirty state and Blueprint compile state and creates no transaction, compile, save, selection, or mutation.

## Verification

`UnrealMCP.Phase8.ActionCatalog` retains coverage for the core families, identities, bounds, expiry, invalidation, stale snapshots, and non-mutation. `UnrealMCP.Phase10.ExpandedActionCatalog` covers unique events, latent restrictions, flow control, casts, literals, common and wildcard operators, event/function/macro graph restrictions, exact and pin-context filters, forged actions, cache reuse, stale snapshots, and non-mutation. The cross-process script verifies every released family after save and editor restart through the production Python client.
