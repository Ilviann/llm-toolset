---
feature_id: reflected-inspect
status: completed
depends_on:
  - asset-inspect-data
released_in: "0.54.0"
---
# Expanded reflected inspection

**Depends on:**

- [`asset-inspect-data`](asset-inspect-data.md)

## Contract

Read safe nested structs, GUID fields, tags, text, enums, soft references and arrays. Unsupported Data Table fields return an explicit unavailable value while supported siblings remain visible. Mutation decoding is unchanged.

## Verification

- [x] Implementation and focused regression fixtures.
- [x] Full Python/native suites, Windows build variants, packaging, and production-bridge restart validation.
- [x] Final release metadata and macOS follow-up tracking.

See [implementation evidence](../../plans/inspection-expansion.md).
