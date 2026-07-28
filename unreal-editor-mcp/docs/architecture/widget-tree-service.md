# Widget-tree service

## Responsibility

`FUnrealMCPWidgetTreeService` is the `widget_tree_edit` facade for one exact `UWidgetBlueprint`. It owns structural operations and delegates UMG layout, styling, property bindings, and Designer events to focused services. `UnrealMCPWidgetTreeInspector.h` emits widget, slot, default, layout, and binding records. `UnrealMCPWidgetTreeSupport.h` owns deterministic traversal and stable tree identities.

## Dependencies

The facade depends on Blueprint family policy, inspector snapshots, mutation preconditions, the reference scanner, public `WidgetTree`/panel/named-slot APIs, and `FWidgetBlueprintOperationUtils`. Layout, style, binding, and reflected-value responsibilities point inward through their own components. The HTTP bridge owns the facade; the Python widget catalog owns model-facing validation. No widget component writes protocol output or saves implicitly.

## Invariants

- Only the published `widget` family is accepted. Actor component operations remain unavailable.
- Widget identities use the Widget Blueprint's persistent name-to-GUID map. Slot identities are deterministic hashes of stable owners, children, and slot names.
- Every edit requires a fresh operation ID and authoritative snapshot and is retained by the shared mutation ledger.
- Each operation accepts one exact shape and changes only selected local tree, layout, presentation, binding, or graph state.
- Root/cycle/container/reference protections run before structural mutations; delegated services apply their narrower allowlists and signature checks.
- Inspection refuses trees over 512 widgets, depth 32, 256 named slots, or 256 bindings.
- Compilation and saving remain explicit Blueprint commands.

## Verification

`UnrealMCP.WidgetTree.FamilyInspectionMutationAndPersistence` covers the structural contract. `UnrealMCP.UMGAuthoring.LayoutStyleBindingsAndEvents` covers typed Canvas layout, Text/ProgressBar presentation, member-property binding, Designer-event creation, inspection, compilation, and saving. Python tests cover exact schemas, registration, versions, capability limits, and focused component size.
