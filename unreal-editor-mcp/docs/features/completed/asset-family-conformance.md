---
feature_id: asset-family-conformance
status: completed
depends_on:
  - asset-inspection-adapters
  - asset-authoring-kernel
  - python-asset-family-catalog
released_in: null
release_track: support-tooling
---

# `asset-family-conformance` — Reusable asset-family verification

**Outcome:** Every built-in or companion asset family can declare bounded fixture
metadata and run shared inspection, authoring, packaging, persistence, recovery,
and unavailable-state gates without copying their implementation.

**Depends on:**

- [`asset-inspection-adapters`](asset-inspection-adapters.md)
- [`asset-authoring-kernel`](asset-authoring-kernel.md)
- [`python-asset-family-catalog`](python-asset-family-catalog.md)

### Implementation

- Added a native fixture runner for independent inspection, creation, and editing
  capability shapes, deterministic frozen identities, unavailable dependencies,
  bounded document/selector/snapshot adapters, and common authoring-kernel
  cleanup, stale-state, rollback, persistence, Undo/Redo, and preservation gates.
- Added immutable Python catalog fixtures for representative Asset, Blueprint,
  Widget, Game Data, and test-companion entries. The common runner checks exact
  access/result policy, unavailable native commands, readonly filtering,
  deterministic schema preservation, and admitted, dormant, not-ready, and
  schema-rejected companion branches.
- Added base-plugin and test-companion package fixtures that compose the normal
  descriptor/binary verifier with exact family module and repository dependency
  checks.
- Added production-bridge fixtures for deterministic identity and snapshot
  checks, selector/page variants, retained committed results after lost
  responses, and restart read-back. The Blueprint, Widget, Game Data, and test
  companion cross-process scenarios use these helpers while retaining focused
  semantic assertions.

### Verification and completion evidence

- `UnrealMCP.AssetFamilies.ConformanceMatrix` applies the native matrix to
  inspection-only, creation-only, edit-only, combined, missing-dependency,
  built-in `core_blueprint`/`neutral_asset`, and Game Data authoring fixtures.
- `tests.test_asset_family_conformance` applies the Python and packaging runners
  to the representative built-in families and test companion, and unit-tests the
  live bridge, restart, preservation, and retained-result helpers.
- The full headless workflow applies the shared bridge gates to built-in
  Blueprint, Widget, and Game Data fixtures plus the test companion. Existing
  focused native and headless tests continue to own family-specific Unreal
  semantics.
- macOS follow-up passed on 2026-08-15 through the native conformance matrix,
  Python fixtures, full production-socket restart workflow, and universal base
  and test-companion packages.
- This is support tooling only: public commands, schemas, capabilities, runtime
  behavior, companion API v1, and plugin versions are unchanged.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
