# UMG layout service

## Responsibility

`FUnrealMCPWidgetLayoutService` owns `set_slot`, live panel-slot property allowlists, encoded layout inspection, and layout snapshot fingerprinting.

## Dependencies

It depends on shared UMG authoring support and public UMG slot classes. The widget-tree facade dispatches requests; the inspector consumes its read-only helpers.

## Invariants

- Requests identify one current local panel slot by stable ID and update one direct allowlisted property.
- Allowlists are slot-class-specific and use exact Unreal property names.
- Canvas, Grid, Uniform Grid, linear/scroll, Overlay/Size Box, and Wrap Box slot families expose only common layout controls.
- Child ordering remains a structural `add`/`reparent` responsibility.
- Unrelated slot fields and children are preserved.

## Verification

The UMG Automation case applies and reads back a nested Canvas `LayoutData`. Schema tests cover exact `set_slot` arguments and bounded recursive values.
