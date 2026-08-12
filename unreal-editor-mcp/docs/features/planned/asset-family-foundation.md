---
feature_id: asset-family-foundation
status: planned
depends_on:
  - asset-inspect-core
  - native-command-catalog
released_in: null
---

# `asset-family-foundation` — Built-in asset-family adapter contracts

**Outcome:** Built-in asset types register through deterministic family descriptors with separate inspection, creation, and editing capabilities.

**Depends on:**

- [`asset-inspect-core`](../completed/asset-inspect-core.md)
- [`native-command-catalog`](../completed/native-command-catalog.md)

### Implementation

- Add stable family IDs, exact class policy, priority, required modules, limits, and independent inspection, creation, and editing capability declarations.
- Introduce typed inspection, creation, and edit adapter interfaces plus bounded contexts, semantic document builders, selector routing, snapshot contribution, and value records.
- Freeze the trusted built-in registry before bridge readiness. Reject ambiguous classification, collisions, late registration, and capability disagreement.
- Keep target resolution, access policy, schemas, persistence authority, and response encoding outside family adapters. Inspection support never implies authoring support.

### Verification and completion gate

- Test exact and derived-class selection, priority, unavailable dependencies, collisions, stable ordering, independent capability combinations, and restart determinism.
- Run native public-boundary probes, the Python suite, all native Automation, headless integration, and base packaging.
- Complete only when a synthetic built-in family can register each capability independently without bridge or unrelated-family changes.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
