# Action queries and records

`blueprint_action_catalog` accepts exactly `asset_path`, `graph_id`, and `expected_snapshot`, plus optional `text`, `owner_class`, `function`, `member`, `node_family`, `pin_context`, and `limit`. `text` matches the complete menu title or member name case-insensitively. Owner, function, and member filters are exact; node family is `function_call`, `variable_get`, or `variable_set`. Pin context contains one exact stable `node_id` and `pin_id` from the selected graph. The default limit is 20 and the maximum is 50.

The target must resolve to an Actor Blueprint and a live graph with the supplied GUID. The inspector recomputes the full structural snapshot before every catalog access; a mismatch returns `stale_precondition`. An unknown graph or pin returns `not_found`. Conflicting function/member/family filters return `invalid_argument`.

A result contains the bridge instance, normalized asset path, graph and snapshot IDs, `actions`, returned/scanned counts, `truncated`, `timed_out`, and the remaining advertised identity lifetime. Each action has an opaque 32-character `action_id`, `node_family`, bounded display title/category, exact owner class and member name, and member kind. Function calls additionally report pure, static, and const flags.

Action IDs are not signatures and cannot be supplied to any released Phase 8 operation. Internally a retained record binds the ID to bridge instance, target generated class, graph schema/GUID, snapshot, normalized query and pin context, plus a rebuildable family/owner/member/spawner signature. Identical queries reuse live IDs. IDs expire after 60 seconds, and are invalid after record eviction, structural change, or bridge restart. At most 32 catalogs and 256 action records are retained.

The target asset/class hierarchy is scanned before the remaining global database. At most 20,000 spawners are examined for at most one second, and one catalog runs at a time on the Game thread. Result-limit, scan-limit, or elapsed-time termination sets `truncated`; elapsed-time termination also sets `timed_out`. The shared 64 KiB request and 256 KiB response ceilings remain authoritative.
