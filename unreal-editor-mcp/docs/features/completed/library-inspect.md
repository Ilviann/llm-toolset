---
feature_id: library-inspect
status: completed
depends_on:
  - asset-inspect-core
released_in: "0.58.0"
---
# Blueprint library inspection

**Depends on:**

- [`asset-inspect-core`](asset-inspect-core.md)

## Contract

Discover Function and Macro Libraries without loading candidates. Inspect declarations, typed signatures, locals and selected graph contents with stable identities and paging. Classify Actor-scoped Macro Libraries before Actor ancestry; reject all authoring operations.

## Verification

- [x] Implementation and focused regression fixtures.
- [x] Full Python/native suites, Windows build variants, packaging, and production-bridge restart validation.
- [x] Final release metadata and macOS follow-up tracking.

See [implementation evidence](../../plans/inspection-expansion.md).
