---
feature_id: reflected-inspection
status: completed
depends_on:
  - phase-4
  - phase-17
released_in: "0.33.0"
---

# `reflected-inspection` — Reflected values, inherited components, and Data Assets

**Outcome:** Agents can inspect richer bounded reflected values, retain useful Data Table fields beside unsupported fields, read generic Data Asset properties, and observe the effective inherited component-template values of a child Blueprint with explicit provenance.

**Status:** Completed in 0.33.0 with Windows verification. macOS verification remains in the native platform backlog.

**Depends on:**

- [`phase-4`](phase-4.md) for component-template and default inspection.
- [`phase-17`](phase-17.md) for Data Table inspection and the bounded game-data value codec.

### Implementation

- Extend read-only reflected encoding for gameplay tags/containers, GUIDs, text, enum values, hard/soft object and class references, arrays including soft references, and bounded nested reflected structs without widening mutation authority.
- Encode unsupported Data Table fields individually so supported siblings remain available; preserve whole-request failures for limits, malformed requests, and unsafe asset boundaries.
- Add `data_asset` to `game_data_inspect` for bounded editable-property inspection of exact `UDataAsset` and `UPrimaryDataAsset` instances, including referenced Data Tables and other compatible assets.
- Resolve inherited SCS nodes against the inspected child generated class, accept ancestor-stable component IDs or exact component names, and expose template/property origins as local, local override, inherited, or native-class provenance.

### Verification

- Cover all new value kinds, unsupported-field isolation, Data Asset references, and unsupported Data Asset properties in `UnrealMCP.ReflectedInspection.GameDataAndDataAssets`.
- Cover child-effective inherited component values, local overrides, source Blueprints, stable ancestor IDs, exact names, and Blueprint reflected default values in focused Phase 4 Automation tests.
- Run Python schema/release tests, documentation lint, adaptive and forced-unity Editor builds, all `UnrealMCP` Automation Tests, and the production headless workflow.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
