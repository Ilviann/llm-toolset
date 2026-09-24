---
feature_id: interface-inspect
status: completed
depends_on:
  - graph-scope-inspect
released_in: "0.60.0"
---
# Interface discovery and authored graph inspection

**Depends on:**

- [`graph-scope-inspect`](graph-scope-inspect.md)

## Contract

Discover Blueprint Interfaces separately from Animation Layer Interfaces. Expose stable declaration identities, typed parameters/metadata, declaration pages and selected authored entry/result graph structure. Interfaces and animation families remain inspection-only.

## Verification

- [x] Implementation and focused regression fixtures.
- [x] Full Python/native suites, Windows build variants, packaging, and production-bridge restart validation.
- [x] Final release metadata and macOS follow-up tracking.

See [implementation evidence](../../plans/inspection-expansion.md).
