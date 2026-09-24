---
feature_id: inherited-inspect
status: completed
depends_on:
  - reflected-inspect
released_in: "0.57.0"
---
# Effective inherited component inspection

**Depends on:**

- [`reflected-inspect`](reflected-inspect.md)

## Contract

Resolve ancestor-stable SCS IDs or exact unambiguous names to the effective child template. Return template path/origin, property declaration/origin, exact property selectors, and snapshots that cover inherited overrides.

## Verification

- [x] Implementation and focused regression fixtures.
- [x] Full Python/native suites, Windows build variants, packaging, and production-bridge restart validation.
- [x] Final release metadata and macOS follow-up tracking.

See [implementation evidence](../../plans/inspection-expansion.md).
