---
feature_id: attribute-map-inspect
status: completed
depends_on:
  - attribute-inspect
released_in: "0.56.0"
---
# Gameplay Attribute collection values

**Depends on:**

- [`attribute-inspect`](attribute-inspect.md)

## Contract

Reuse the typed attribute reader in game-data values and map keys, preserving canonical reflected map ordering and UE 5.7 three-field exported attribute syntax.

## Verification

- [x] Implementation and focused regression fixtures.
- [x] Full Python/native suites, Windows build variants, packaging, and production-bridge restart validation.
- [x] Final release metadata and macOS follow-up tracking.

See [implementation evidence](../../plans/inspection-expansion.md).
