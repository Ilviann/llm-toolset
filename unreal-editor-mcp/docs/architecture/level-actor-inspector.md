# Level actor inspector

## Ownership

`UnrealMCPLevelActorInspector.h/.cpp` owns current-map actor query validation, World Partition descriptor enumeration, exact actor resolution, actor/component record encoding, stable actor-scoped component identities, and requested reflected-property reads. `UnrealMCPLevelService` supplies the authoritative current world, map identity, snapshot, pagination, and cursor lifecycle.

## Dependency direction

Broad World Partition queries read `FWorldPartitionActorDescInstance` metadata and never create actor references. Exact actor/component queries may hold one scoped `FWorldPartitionReference` long enough to read live data. Non-partitioned maps use loaded persistent-level actors. Reflected values reuse `UnrealMCPGameDataValueCodec`; the inspector adds the UObject property visibility and safety gate before encoding.

## Invariants

- Every actor query is bound to the exact current 40-hex `map_id` and `expected_snapshot`; Actor GUID identities are qualified as `map_id:actor_guid`.
- `actors` scans at most 4,096 descriptors or loaded actors, retains at most 2,048 matches, sorts by actor identity, and relies on the level service for pages and single-use cursors.
- Broad World Partition inspection uses descriptors only. Only `actor` and `component` may load, and only the exact requested actor through one scoped reference. Loaded state requires current level registration, so an unregistered actor UObject awaiting garbage collection remains logically unloaded.
- Filters are exact for identity, label, class path, tag, folder, data layer, loaded state, and intersecting finite region.
- Actor records distinguish descriptor availability and loaded state, report bounded tags/data layers, package and dirty state, transform/bounds, spatial settings, and attachment identity.
- Component identities are deterministic within an actor and report native-default, Blueprint-created, construction-script, or instance origin. More than 64 live components is an explicit bounded failure.
- Only requested editable or Blueprint-visible scalar/structured values accepted by the shared recursive codec are returned. Transient, deprecated, editor-only, delegate, interface, exported, instanced-object-graph, and other unsupported properties fail as `unsupported_property`.
- Inspection creates no transaction, selection change, save, dirtying, or loaded-region change. A scoped targeted reference is released before the request returns.

## Verification

Run the Python contract suite, build adaptive and forced-unity Editor targets, run `UnrealMCP.LevelInspect`, then run `scripts/run_headless_integration.py`. The native fixture covers exact filters, descriptor fields, identities/origins, actor and component properties, unsupported properties, pagination, stale snapshots, and preservation of selection, dirtiness, and loaded regions. The production workflow covers descriptor continuation, region filtering, exact live property inspection, and actor identity across restart.
