# Widget compatibility

## Ownership

`UnrealMCPWidgetCompatibility.h/.cpp` owns the narrow Unreal Engine 5.7 public-API facade used by Widget Blueprint creation, tree mutation, variable exposure, and Designer-event binding. It replaces the later-engine `FWidgetBlueprintOperationUtils` surface without exposing a second model-facing contract. `UnrealMCPBlueprintMutatorLifecycle`, `UnrealMCPWidgetTreeService`, and `UnrealMCPWidgetBindingService` remain responsible for MCP request validation, stale/reference preflight, operation-ledger behavior, transaction scope, compilation, saving, and result read-back.

## Dependency direction

The facade composes public `FKismetEditorUtilities`, `FBlueprintEditorUtils`, `FWidgetBlueprintEditorUtils`, `UWidgetTree`, panel/named-slot, Widget Animation/Movie Scene, and UMG editor APIs available in Unreal 5.7. Service components call the facade; the facade does not depend on the bridge, protocol, request schemas, services, or operation ledger. `UnrealMCPApiProbe.cpp` takes addresses of the facade entry points so the normal module build verifies their signatures and public-header dependencies.

## Invariants

- Creation accepts only a `UUserWidget` parent class, optionally creates a panel root, applies the project initialization setting, broadcasts Widget Blueprint creation, and registers/compiles only when requested.
- Add and move validate tree ownership, panel capacity, root uniqueness, insertion position, and parent/child cycles before structural modification. Move preserves compatible slot properties.
- Remove uses Unreal's public Widget Blueprint deletion path; rename validates live object, Blueprint, inherited `BindWidget`, navigation, binding, animation, and variable-reference state before structural read-back.
- Variable exposure and component-bound events operate only on the exact live widget/skeleton properties selected by the owning service.
- Transient failed creations are detached from the tree and moved out of the asset package without redirectors.
- The facade is compatibility code for the exact Unreal 5.7 API line. Another engine line requires its own branch and equivalent native behavior tests.

## Verification

Build both adaptive/non-unity and forced-unity `UnrealMCPTestEditor` targets. `UnrealMCP.WidgetTree.FamilyInspectionMutationAndPersistence` covers creation, add, move, remove, rename, variable exposure, cycles, transactions, compilation, save, and reload read-back. `UnrealMCP.UMGAuthoring.LayoutStyleBindingsAndEvents` covers property binding and Designer-event creation/removal. The complete `UnrealMCP` native suite and cross-process widget scenario guard integration with compilation, persistence, and the production bridge.
