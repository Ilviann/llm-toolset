---
feature_id: reflected-map-gameplay-attributes
status: completed
depends_on:
  - gameplay-attribute-inspect
  - reflected-inspection
released_in: "0.36.0"
---

# `reflected-map-gameplay-attributes` — Reflected maps and Gameplay Attribute compatibility

**Outcome:** Agents can inspect bounded maps on Blueprint component/class defaults and receive complete typed Gameplay Attribute identities from UE 5.7 exports and game-data containers, including map keys.

**Status:** Completed in 0.36.0 with Windows verification. macOS verification remains in the native platform backlog.

**Depends on:**

- [`gameplay-attribute-inspect`](gameplay-attribute-inspect.md) for the shared reflection-only typed Gameplay Attribute representation.
- [`reflected-inspection`](reflected-inspection.md) for bounded Blueprint and game-data reflected-value codecs.

### Implementation

- Accept the bounded UE 5.7 Gameplay Attribute export fields `AttributeName`, `Attribute`, and `AttributeOwner`, while preferring the live field path when it is present.
- Reuse the shared typed Gameplay Attribute encoder in Data Table and Data Asset inspection, recursively including collection entries and map keys.
- Add read-only Blueprint component/class-default maps when both key and value types are already readable, cap them at 64 entries, and sort their canonical encoded entries for deterministic snapshots.
- Keep set inspection and all map mutation outside the Blueprint property codec; retain existing collection, depth, response, and non-mutation bounds.

### Verification

- Exercise real engine Gameplay Attribute exports in K2 and reflected component defaults, deterministic Blueprint map values, and a typed Gameplay Attribute Data Asset map key in Phases 4, 5, and 17.
- Run adaptive and forced-unity Editor builds, focused and full native Automation, Python/release tests, documentation lint, base/GAS packaging, and production headless workflow on Windows.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
