# UMG asset inspection

## Ownership

`UnrealMCPUMGInspectionAdapter.cpp` owns the built-in composable `umg_widget` family descriptor, UMG selector routing, root and selected semantic records, and binding/navigation projection. `UnrealMCPUMGInspectionModel.cpp` owns effective Widget Blueprint hierarchy, inherited/local provenance, named-slot and binding collection, allowlisted Widget/slot properties, safety admission, and the UMG snapshot contribution. Core Blueprint adapters continue to own asset identity, variables, functions, macros, events, and graph traversal.

## Dependency direction

The UMG domain registers its overlay against the represented generated `UUserWidget` class during built-in family composition. It depends inward on Asset Core family builders and Blueprint structured-data, widget-tree, graph, property, and binding helpers. It has no dependency on the host bridge, MCP framing, editor UI, runtime widget instances, CommonUI, MVVM, MovieScene, mutation, compilation, or persistence.

## Invariants

- Root inspection composes with `core_blueprint`; targeted `widget_tree`, `widgets`, `named_slots`, `bindings`, and `properties` routes belong only to the UMG overlay.
- The effective tree is parent-before-child, uses widget names as percent-encoded selector keys, and identifies every record as `local` or `inherited` with exact declaring-class provenance. Duplicate names fail closed.
- Inspection admits at most 512 widgets, depth 32, 256 named slots, and 256 bindings. Paging bounds response size but does not admit larger assets.
- Widget and panel-slot properties come from explicit base-UMG allowlists. Collection values remain behind exact nested selectors; unsafe delegates, instanced objects, editor/runtime state, bulk data, and unsupported values do not receive generic reflection.
- Navigation is flattened to direction/rule/target records. Legacy bindings include source/target types and polling cost; Designer bindings include graph/event selectors, delegate signature, and event-driven cost.
- Widget Animation timelines, CommonUI-specific data, MVVM data, Slate/runtime instances, viewport state, and gameplay objects are excluded. CommonUI may compose its separate admitted companion overlay without changing the base result.
- The query-independent asset snapshot includes core Blueprint and UMG contributions. Inspection does not dirty packages, compile, save, instantiate widgets, or mutate Blueprint status.

## Verification

`UnrealMCP.AssetInspect.UMGHierarchyLayoutBindingsAndExclusions` covers local and inherited trees, deterministic paging, navigation, class/style defaults, Canvas layout, named slots, both binding kinds, exact collection selectors, exclusions, snapshots, and dirty-state preservation. Existing Widget Tree and UMG authoring Automation covers every supported slot/property mutation and stable selector identity. Python contracts, the headless restart fixture, production-socket lifecycle, three Windows build modes, and base Win64 packaging cover publication and distribution.
