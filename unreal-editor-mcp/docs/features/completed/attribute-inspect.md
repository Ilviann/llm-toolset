---
feature_id: attribute-inspect
status: completed
depends_on:
  - reflected-inspect
released_in: "0.55.0"
---
# Reflected Gameplay Attributes

**Depends on:**

- [`reflected-inspect`](reflected-inspect.md)

## Contract

Read exact GameplayAttribute structs and Blueprint variable/pin exports through reflection only. Return resolution, compatibility, attribute name, property path and owner path. Never load referenced assets or depend on GameplayAbilities in the base plugin.

## Verification

- [x] Implementation and focused regression fixtures.
- [x] Full Python/native suites, Windows build variants, packaging, and production-bridge restart validation.
- [x] Final release metadata and macOS follow-up tracking.

See [implementation evidence](../../plans/inspection-expansion.md).
