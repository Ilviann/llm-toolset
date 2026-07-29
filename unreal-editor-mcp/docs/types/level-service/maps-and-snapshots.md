# Maps, revisions, and snapshots

`level_inspect` accepts one of six exact shapes:

- `{"mode":"discover",...}` with optional mounted `package_path`, exact `asset_name`, and page size;
- `{"mode":"current"}`; or
- `{"mode":"actors","map_id":...,"expected_snapshot":...,...}`;
- `{"mode":"actor","map_id":...,"expected_snapshot":...,"actor_id":...,...}`;
- `{"mode":"component","map_id":...,"expected_snapshot":...,"actor_id":...,"component_id":...,...}`; or
- a single-use 32-hex cursor plus optional page size.

Discovery records contain `section:"map"`, exact World `map_path`, package/name/mount fields, and Asset Registry World Partition/external-actor flags. Scans stop at 2,048 candidates. Pages contain at most 100 records; up to 32 cursors live for 30 seconds and are bound to the normalized query and 40-hex snapshot.

The current record contains `section:"current_map"`, project-qualified `map_id`, exact path/package/name/mount, 40-hex `map_revision`, mounted/dirty/completeness flags, bounded dirty-package names, loaded external-package count, and live World Partition/external-actor flags. `snapshot_id` binds the current map identity and revision. Untitled/template worlds remain inspectable but report `mounted:false` and cannot be passed to `level_open`.

Actor, component, and property queries require this exact current `map_id` and `snapshot_id` as `expected_snapshot`. Their complete record and safety contracts are in [`actors-components-and-properties.md`](actors-components-and-properties.md).

`level_open` accepts exactly `operation_id` and `map_path`. `map_path` is an Asset Registry World object path, never a filesystem path. The operation ledger provides queued/executing/committed/rejected state, digest binding, replay, conflict rejection, timeout reconciliation, 128-record capacity, and 15-minute retention.

Opening the current exact map returns `already_current:true` without reload, even when it is dirty. Switching maps requires a completely inspected clean current map and safe editor state. Success returns `opened`, `already_current`, the exact snapshot, and a full `current_map` read-back. Unsafe state returns `busy`; missing/non-World/non-exact targets return stable argument/type/not-found errors.
