---
feature_id: python-asset-family-catalog
status: planned
depends_on:
  - asset-family-foundation
released_in: null
---

# `python-asset-family-catalog` — Static Python asset-family catalog

**Outcome:** Approved built-in and companion families compose exact Python schemas, access gates, operation mappings, and capability requirements from one static catalog.

**Depends on:**

- [`asset-family-foundation`](../completed/asset-family-foundation.md)

### Implementation

- Define shipped family entries for schema branches, readonly/writable filtering, native capability requirements, operation mappings, and result handling.
- Keep the Python/base release authoritative for every model-facing schema. Native modules and companions cannot provide schemas dynamically.
- Preserve the compact public tool surface and existing tool names; family-specific semantics remain exact discriminated branches rather than loose argument objects.
- Remove duplicate family knowledge from server dispatch, schema validation, access filtering, and extension-catalog composition.

### Verification and completion gate

- Test deterministic catalog composition, duplicate/conflicting entries, unavailable native capabilities, readonly filtering, exact schemas, and unchanged MCP initialization/list/call behavior.
- Run the complete Python suite plus production-socket integration against matching native capabilities.
- Complete only when an approved family requires one catalog entry and no unrelated Python server changes.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
