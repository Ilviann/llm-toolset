---
feature_id: asset-inspect-data
status: planned
depends_on:
  - asset-inspect-core
  - native-domain-modules
released_in: null
---

# `asset-inspect-data` — Data Asset and Data Table inspection

**Outcome:** The established `asset_inspect` tool can analyze structured project data through the same deterministic YAML, selector, paging, snapshot, and error contracts as gameplay Blueprints.

**Depends on:**

- [`asset-inspect-core`](../completed/asset-inspect-core.md)
- [`native-domain-modules`](native-domain-modules.md)

### Family scope

- Add deep inspection for Data Assets, Primary Data Assets, Blueprint and Data-Only Blueprint Data Asset class variants, and Data Tables according to the accepted [family contracts](../../types/asset-inspection/asset-types/index.md).
- Inspect Data Asset framework identity, Primary Asset identity and bundles, class defaults or instance values, cumulative safe reflected properties, and exact pageable selectors for every array, set, map, and nested collection.
- Inspect Data Table row-struct identity, import policy, typed schema, compact row index, zero-based pageable row detail, nested row collections, and optional `columns/<field>` projections.
- Reuse the core property/type/snapshot/page infrastructure and the existing game-data schema and value codecs. Do not add another model-facing tool, widen recursive object traversal, emit source CSV/JSON, or alter the common request shape.

### Verification and completion gate

- Test native and user-defined row structs, empty and large tables, deterministic row and column pages, arrays/sets/maps, nested collections, Data Asset inheritance, Primary Asset bundles, unsupported instanced objects, stale snapshots, limits, YAML correspondence, and read-only preservation.
- Run the complete core regression suite plus mandatory Windows native, headless, production-socket, and packaging verification.
- Complete only when every advertised data family obeys the shared selector/page/snapshot contract, unsupported values fail visibly and safely, existing `game_data_inspect` remains compatible, and capabilities match live dispatch.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
