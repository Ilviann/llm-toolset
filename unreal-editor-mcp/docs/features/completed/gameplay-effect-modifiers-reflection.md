---
feature_id: gameplay-effect-modifiers-reflection
status: completed
depends_on:
  - gameplay-attribute-inspect
  - gas-gameplay-effects-inspect
  - reflected-inspection
released_in: "0.35.0"
---

# `gameplay-effect-modifiers-reflection` — Reflected Gameplay Effect modifiers

**Outcome:** Agents can request the exact `Modifiers` class default on an admitted Gameplay Effect Blueprint and inspect its bounded `FGameplayModifierInfo` array instead of receiving an unsupported-property record.

**Status:** Completed in 0.35.0 with Windows verification. macOS verification remains in the native platform backlog.

**Depends on:**

- [`gameplay-attribute-inspect`](gameplay-attribute-inspect.md) for nested typed Gameplay Attribute values.
- [`gas-gameplay-effects-inspect`](gas-gameplay-effects-inspect.md) for admission of the read-only Gameplay Effect Blueprint family.
- [`reflected-inspection`](reflected-inspection.md) for bounded targeted generated-class default inspection.

### Implementation

- Recognize only arrays whose exact live element type is `/Script/GameplayAbilities.GameplayModifierInfo`, through reflection and without adding a GameplayAbilities dependency to the base plugin.
- Retain the ordinary published reflected-data depth limit while granting this exact array the two additional internal levels required by magnitude curves and tag queries.
- Encode bounded modifier attributes, operations, magnitude backing records, evaluation channels, and source/target tag requirements with the existing read-only reflected-value format and collection/response ceilings.
- Keep the dedicated `gameplay_effect_modifiers` companion record as the concise semantic view of active magnitude forms, ownership, stable modifier identities, and duplicates.

### Verification

- Construct a live reflected Gameplay Effect modifier, assign a resolved Gameplay Attribute, and prove the array plus nested magnitude and tag-requirement fields encode through `UnrealMCP.Phase5.GameplayEffectModifiers`.
- Run the adaptive Editor build, focused and full native Automation, Python/release tests, documentation lint, base/GAS packaging, and production headless workflow on Windows.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
