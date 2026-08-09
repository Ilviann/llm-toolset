---
feature_id: asset-inspect-umg
status: planned
depends_on:
  - asset-inspect-core
released_in: null
---

# `asset-inspect-umg` — Base UMG Widget Blueprint inspection

**Outcome:** The established `asset_inspect` tool can analyze base UMG Widget Blueprint logic, hierarchy, layout, presentation, and bindings through the common semantic inspection contract.

**Depends on:**

- [`asset-inspect-core`](asset-inspect-core.md)

### Family scope

- Add deep inspection for `UWidgetBlueprint` assets derived from `UUserWidget` according to the accepted [Widget Blueprint contract](../../types/asset-inspection/asset-types/widget-blueprint.md).
- Reuse core Blueprint variables, functions, macros, events and graph normalization, then add effective inherited/local widget hierarchy, panel-slot layout, bounded styles and presentation, named slots, legacy property bindings, and Designer event bindings.
- Keep collection-valued defaults and large semantic indexes behind exact zero-based pageable selectors while widget logic graphs remain atomic.
- Exclude Widget Animation timelines, bindings, tracks, sections and keyframes. Also exclude CommonUI-specific and MVVM-specific records; base UMG inspection remains available when ordinary `UUserWidget` ancestry is supported.

### Verification and completion gate

- Test inherited and local widget trees, duplicate-name handling, selector escaping, each supported panel slot, style/property values, named slots, legacy and Designer bindings, pageable indexes, graph selectors, unsupported custom widgets, exclusions, and read-only preservation.
- Run the complete core regression suite plus mandatory Windows UMG Automation, headless, production-socket, and packaging verification.
- Complete only when hierarchy and ownership remain unambiguous across pages, logic uses the shared graph contract, excluded animation/CommonUI/MVVM data cannot leak, and capabilities match live dispatch.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
