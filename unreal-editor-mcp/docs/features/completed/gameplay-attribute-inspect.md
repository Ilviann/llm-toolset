---
feature_id: gameplay-attribute-inspect
status: completed
depends_on:
  - phase-5
  - reflected-inspection
released_in: "0.34.0"
---

# `gameplay-attribute-inspect` — Gameplay Attribute value inspection

**Outcome:** Agents can inspect scalar `FGameplayAttribute` identities in Blueprint member/pin defaults and targeted reflected defaults instead of receiving an unavailable or unsupported struct value.

**Status:** Completed in 0.34.0 with Windows verification. macOS verification remains in the native platform backlog.

**Depends on:**

- [`phase-5`](phase-5.md) for canonical K2 member and pin default inspection.
- [`reflected-inspection`](reflected-inspection.md) for bounded targeted reflected-property values.

### Implementation

- Recognize the exact live `/Script/GameplayAbilities.GameplayAttribute` struct through reflection without linking GameplayAbilities into the base plugin.
- Encode the attribute's live resolution, supported-property compatibility, name, property path, and owner path through one bounded shared helper.
- Use the typed value in K2 variable/pin defaults and targeted reflected component/class defaults while leaving mutation unsupported.
- Preserve explicit `unavailable` output for malformed serialized defaults and the existing reflected unsupported sentinel when encoding cannot be completed safely.

### Verification

- Cover a real engine Attribute Set property, malformed default handling, a compiled Blueprint member default, and targeted generated-class CDO read-back in `UnrealMCP.Phase5`.
- Run the adaptive Editor build, focused and full native Automation, Python/release tests, documentation lint, base-plugin packaging, and production headless workflow on Windows.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
