# Blueprint action catalog

## Ownership

`UnrealMCPBlueprintActionCatalog` owns the read-only `blueprint_action_catalog` command after Game-thread dispatch. It resolves one exact Actor Blueprint graph and structural snapshot, scans Unreal's public `FBlueprintActionDatabase`, applies a live `FBlueprintActionFilter` for the Blueprint, graph, and optional pin, and returns only supported function-call and variable get/set spawners. The target Blueprint and its class hierarchy are scanned first so useful local and inherited actions remain available within the global scan budget.

## Dependency direction

The editor bridge constructs the catalog with the bridge-instance identity and shared inspector. The inspector supplies the authoritative structural snapshot. The catalog depends on public Blueprint action database/filter/spawner APIs and live K2 graph objects; it does not depend on the mutator, transactions, compilation, saving, selection, or editor UI.

## Invariants

- Every query requires one exact Actor Blueprint asset path, stable graph GUID, and current structural snapshot.
- Optional filters are exact case-insensitive text, owner-class path, function name, member name, node family, and one exact node/pin context. Function and variable filters cannot be combined incompatibly.
- Only `function_call`, `variable_get`, and `variable_set` records are released. Callers never supply a node class, field signature, or spawner and cannot invoke an action in Phase 8.
- Every action passes Unreal's live Blueprint/graph/pin action filter. Results expose descriptive metadata, not unrestricted construction data.
- Opaque action IDs are process-local, bound through the retained query to bridge instance, target generated class, graph schema and GUID, structural snapshot, normalized filters, pin context, and an internal rebuildable action signature.
- Identical live queries reuse retained IDs. Records expire after 60 seconds; expiry, capacity eviction, snapshot changes, and bridge restart invalidate them.
- A query returns at most 50 results, scans at most 20,000 database spawners for at most one second, retains at most 32 catalogs and 256 actions, and permits one Game-thread catalog operation at a time. Global request/response and queue limits still apply.
- Cataloging preserves package dirty state and Blueprint compile state and creates no transaction, compile, save, selection, or mutation.

## Verification

`UnrealMCP.Phase8.ActionCatalog` covers pure/impure and static/instance functions, local/inherited ownership, variable access, exact filters, live pin context, truncation, timeout, cache reuse, expiry, bridge-instance invalidation, forged names, stale snapshots, and non-mutation. The cross-process script verifies local function and variable actions after save and editor restart through the production Python client.
