# UMG style service

## Responsibility

`FUnrealMCPWidgetStyleService` owns `set_style` and computes the live bounded presentation-property allowlist for each widget class.

## Dependencies

It depends on shared UMG authoring support plus public UMG widget classes. The inspector publishes supported property names and the tree facade dispatches mutations.

## Invariants

- Common presentation properties and explicit widget-class-specific properties are the only candidates.
- Live reflection further requires editable, direct, non-delegate, non-container, non-instanced properties supported by the recursive codec.
- The service modifies one property on one exact local widget and preserves unrelated defaults and tree state.
- Presentation editing does not mutate runtime widget instances or gameplay state.

## Verification

The UMG Automation case changes TextBlock text and ProgressBar percent and validates authoritative inspection. Python tests reject arbitrary extra fields and over-deep values.
