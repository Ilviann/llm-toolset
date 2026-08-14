---
feature_id: asset-inspect-umg
status: completed
depends_on:
  - asset-inspect-core
  - native-domain-modules
released_in: "0.48.0"
---

# `asset-inspect-umg` — Base UMG Widget Blueprint inspection

**Outcome:** The established `asset_inspect` tool can analyze base UMG Widget Blueprint logic, hierarchy, layout, presentation, and bindings through the common semantic inspection contract.

**Depends on:**

- [`asset-inspect-core`](asset-inspect-core.md)
- [`native-domain-modules`](native-domain-modules.md)

### Family scope

- Add deep inspection for `UWidgetBlueprint` assets derived from `UUserWidget` according to the accepted [Widget Blueprint contract](../../types/asset-inspection/asset-types/widget-blueprint.md).
- Reuse core Blueprint variables, functions, macros, events and graph normalization, then add effective inherited/local widget hierarchy, panel-slot layout, bounded styles and presentation, named slots, legacy property bindings, and Designer event bindings.
- Keep collection-valued defaults and large semantic indexes behind exact zero-based pageable selectors while widget logic graphs remain atomic.
- Exclude Widget Animation timelines, bindings, tracks, sections and keyframes. Also exclude CommonUI-specific and MVVM-specific records; base UMG inspection remains available when ordinary `UUserWidget` ancestry is supported.

### Implementation

- `UnrealMCPUMGInspectionAdapter.cpp` registers the built-in composable `umg_widget` semantic family for `UUserWidget` generated classes. Core Blueprint inspection continues to own variables and graph selectors; the UMG overlay owns Widget Blueprint defaults, effective hierarchy, layout, styles, named slots, bindings, and its snapshot contribution.
- `UnrealMCPUMGInspectionModel.cpp` builds the deterministic parent-before-child effective tree, inherited/local provenance, safe reflected property allowlists, named-slot relationships, binding semantics, and bounded fingerprint without loading runtime widget instances.
- The shared structured-data boundary provides allowlisted property pages and snapshot material so collection defaults retain exact nested selectors without exposing unrelated reflection.

### Verification

- `UnrealMCP.AssetInspect.UMGHierarchyLayoutBindingsAndExclusions` covers local and inherited hierarchy/provenance, navigation, Canvas layout, style/property values, named slots, legacy and Designer bindings, nested collection paging, exclusions, and read-only preservation. Existing UMG authoring tests cover every supported panel slot and selector identities; core AssetInspect tests retain graph selectors, limits, and escaping.
- Python contracts verify the domain feature, adapter boundary, public classification, production-socket fixture, and packaged module. Release validation runs the complete Python/native suites, headless production socket, adaptive/forced-unity/non-unity Windows builds, and base Win64 packaging.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
