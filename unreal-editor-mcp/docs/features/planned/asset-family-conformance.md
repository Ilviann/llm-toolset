---
feature_id: asset-family-conformance
status: planned
depends_on:
  - asset-inspection-adapters
  - asset-authoring-kernel
  - python-asset-family-catalog
released_in: null
release_track: support-tooling
---

# `asset-family-conformance` — Reusable asset-family verification

**Outcome:** Every built-in or companion asset family proves the common inspection, creation, editing, persistence, and unavailable-state contracts through reusable fixtures and checks.

**Depends on:**

- [`asset-inspection-adapters`](../completed/asset-inspection-adapters.md)
- [`asset-authoring-kernel`](../completed/asset-authoring-kernel.md)
- [`python-asset-family-catalog`](../completed/python-asset-family-catalog.md)

### Implementation

- Add native, Python, packaging, and cross-process conformance harnesses parameterized by family fixtures and expected capabilities.
- Cover identity, selectors, paging, snapshots, limits, deterministic encoding, read-only preservation, creation cleanup, stale editing, transaction recovery, persistence, replay, lost responses, restart read-back, and unrelated-content preservation.
- Support inspection-only, creation-only, edit-only, combined, missing-dependency, and rejected-companion states without weakening family-specific tests.

### Verification and completion gate

- Apply the harness to representative built-in Blueprint/Widget/Game Data families and the test companion.
- Run documentation lint and the complete affected native, Python, headless, build, and packaging suites.
- Complete only when a new family can opt into the common gates without copying their implementation.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
