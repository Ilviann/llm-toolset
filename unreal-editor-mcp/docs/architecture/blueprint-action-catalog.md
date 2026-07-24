# Blueprint action catalog

## Ownership

`FUnrealMCPBlueprintActionCatalog` is the read-only `blueprint_action_catalog` facade after Game-thread dispatch. A typed query decoder owns exact filter normalization, a focused scanner owns target-first database traversal plus family classification/record encoding, and the facade owns live Blueprint/graph/pin resolution plus retained catalog/action identity. The scanner releases function-call, variable get/set, event, flow-control, cast, literal, and operator families through one classifier. Its Phase 11 invocation resolver rebuilds one retained signature from the live database for the graph editor without making the catalog command mutating.

## Dependency direction

The editor bridge constructs the catalog facade with the bridge-instance identity and shared inspector. The inspector supplies the authoritative structural snapshot. Query decoding has no live-object dependency; live resolution precedes scanning; scanning depends on public Blueprint action database/filter/spawner APIs and resolved K2 context; retained-cache management consumes encoded candidate records. The graph editor depends on the catalog's narrow live-resolution method; the catalog does not depend on the graph editor, mutator, transactions, compilation, saving, selection, or editor UI.

## Invariants

- Every query requires one exact supported Blueprint-family asset path, stable graph GUID, and current structural snapshot. Results report the resolved family.
- Optional filters are exact case-insensitive text, owner-class path, function name, member name, node family, and one exact node/pin context. Function and variable filters cannot be combined incompatibly.
- Only `function_call`, `variable_get`, `variable_set`, `event`, `flow_control`, `cast`, `literal`, and `operator` records are released. Callers never supply a node class, field signature, or spawner; Phase 11 `add_node` accepts only the opaque retained ID.
- Event spawners are accepted only for real event functions and are suppressed when their unique event already exists. Flow control covers direct supported K2 flow nodes and context-valid standard flow-control macro actions. Literal and operator classification precedes generic function-call classification.
- Unreal's live action filter remains authoritative for graph kind, Blueprint family, latent availability, static/instance context, inheritance, and optional pin compatibility. Promotable operators remain observable as wildcard candidates without specializing them.
- Every action passes Unreal's live Blueprint/graph/pin action filter. Results expose descriptive metadata, not unrestricted construction data.
- An exact loaded `owner_class` filter is traversed before the target Blueprint hierarchy and global registry, keeping focused native/static-function queries deterministic within the same scan bounds on every host.
- Opaque action IDs are process-local, bound through the retained query to bridge instance, target generated class, graph schema and GUID, structural snapshot, normalized filters, pin context, and an internal rebuildable action signature.
- Before invocation, the retained signature's exact loaded owner is traversed first, then the target hierarchy and live action database are scanned under the same bounds and graph/pin filter. A missing, expired, mismatched, filtered, or unresolvable identity returns `invalid_action` before a transaction.
- Identical live queries reuse retained IDs. Records expire after 60 seconds; expiry, capacity eviction, snapshot changes, and bridge restart invalidate them.
- A query returns at most 50 results, scans at most 20,000 database spawners for at most one second, retains at most 32 catalogs and 256 actions, and permits one Game-thread catalog operation at a time. Global request/response and queue limits still apply.
- Cataloging preserves package dirty state and Blueprint compile state and creates no transaction, compile, save, selection, or mutation.

## Verification

`UnrealMCP.Phase8.ActionCatalog` retains coverage for the core families, identities, bounds, expiry, invalidation, stale snapshots, and non-mutation. `UnrealMCP.Phase10.ExpandedActionCatalog` covers expanded graph-action families and restrictions. `UnrealMCP.Phase14` adds GameMode/GameState inherited framework-call coverage; `UnrealMCP.Phase15` adds GameInstance Init/Shutdown callback coverage. The cross-process script verifies every released family after save and editor restart through the production Python client.
