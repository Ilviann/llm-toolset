---
feature_id: asset-inspection-adapters
status: planned
depends_on:
  - asset-family-foundation
released_in: null
---

# `asset-inspection-adapters` — Asset inspection service decomposition

**Outcome:** `asset_inspect` orchestration is family-independent and existing core asset semantics live in focused inspection adapters.

**Depends on:**

- [`asset-family-foundation`](../completed/asset-family-foundation.md)

### Implementation

- Reduce the inspection service to exact target resolution, family selection, request orchestration, snapshot stability, read-only preservation, and final result encoding.
- Move neutral/media, gameplay Blueprint, standalone Actor Component Blueprint, Blueprint Interface, graph, collection, and semantic-property behavior into focused adapters and shared typed collaborators.
- Preserve the released request, deterministic YAML, selector, paging, graph, snapshot, limit, and error contracts exactly.
- Require new built-in families to add only their descriptor, adapter, static Python catalog entry, fixtures, and documentation.

### Verification and completion gate

- Re-run every `asset-inspect-core` native, Python, UTF-8, YAML, headless, restart, non-mutation, build, and packaging check.
- Add adapter-isolation tests proving unsupported and unrelated families are unchanged.
- Complete only when the coordinator contains no family-specific classification or semantic collection logic.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
