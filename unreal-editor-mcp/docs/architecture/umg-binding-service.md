# UMG binding service

## Responsibility

`FUnrealMCPWidgetBindingService` owns exact property binding/unbinding, Designer-event node binding/unbinding, binding inspection records, and binding snapshot fingerprints.

## Dependencies

It depends on shared UMG authoring support, Blueprint member/graph identity helpers, `FEditorPropertyPath`, and `FWidgetBlueprintOperationUtils`. Existing action discovery and graph editing own all logic connected after a Designer-event node.

## Invariants

- Target widgets must be local, stable-ID-resolved, and variable-exposed.
- Property bindings target one live bindable delegate and one signature-compatible member property or pure/const zero-argument function.
- Property binding records declare `cost: "polling"`; Designer events declare `cost: "event_driven"`.
- Binding replacement/removal preserves every unrelated `UWidgetBlueprint::Bindings` entry.
- Event unbinding requires `reject_if_connected` and refuses graph-connected nodes.
- Inspection and fingerprints refuse more than 256 combined widget bindings.

## Verification

The UMG Automation case binds ProgressBar percent to a float member, creates a Button `OnClicked` event, inspects both records and identities, then compiles and saves. The full native suite protects existing graph and Widget-tree behavior.
