# Widget-tree service

## Responsibility

`FUnrealMCPWidgetTreeService` owns authenticated `widget_tree_edit` execution for one exact `UWidgetBlueprint`. `UnrealMCPWidgetTreeInspector.h` extends the existing inspector builder with widget, panel-slot, named-slot, and targeted default records. `UnrealMCPWidgetTreeSupport.h` owns deterministic traversal and stable identity helpers.

## Dependencies

The service depends on the shared Blueprint family policy, inspector snapshots, mutation path/precondition helpers, property codec, reference scanner, public `WidgetTree`/`UPanelWidget`/`INamedSlotInterface` APIs, and `FWidgetBlueprintOperationUtils`. The HTTP bridge owns the service facade; the Python widget schema module owns model-facing request validation. No widget component writes protocol output or saves implicitly.

## Invariants

- Only the published `widget` family is accepted. Actor component operations remain unavailable.
- Widget identities use the Widget Blueprint's persistent name-to-GUID map. Panel-slot and named-slot identities are deterministic hashes of stable owners, children, and slot names.
- Every edit requires a fresh operation ID and authoritative 40-character Blueprint snapshot. Operations are retained by the shared mutation ledger.
- `set_root`, `add`, `remove`, `rename`, `reparent`, `set_variable`, and `set_property` each accept one exact shape and change only the selected local tree/default state.
- Root removal/reparenting, cycles, occupied named slots, invalid classes, duplicate names, incompatible panel children, referenced destruction, and unsupported reflected properties fail before unsafe mutation.
- Inspection refuses trees over 512 widgets, depth 32, or 256 named slots. It exposes at most 16 changed defaults per widget and refuses more than 1,024 total changed defaults.
- Compilation and saving remain explicit Blueprint commands.

## Verification

`UnrealMCP.WidgetTree.FamilyInspectionMutationAndPersistence` covers family policy, specialized creation, tree/default inspection, stable identities, transactions and undo/redo, stale state, reference refusal, component rejection, compilation, deletion, saving, and read-back. Python tests cover exact schemas, registration, version/capability contracts, and cross-process scenario wiring. The headless workflow authors and saves a tree, restarts the editor, and verifies its stable hierarchy and changed default.
