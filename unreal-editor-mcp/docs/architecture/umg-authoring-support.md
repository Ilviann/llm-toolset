# UMG authoring support

## Responsibility

`UnrealMCPWidgetAuthoringSupport.h/.cpp` centralizes exact-field validation, stale-safe Widget Blueprint resolution, stable widget/slot lookup, bounded reflected-value application and read-back, and common mutation-result construction.

## Dependencies

The component depends on the Widget tree identity helpers, Blueprint mutation preconditions, JSON request values, and the recursive Game Data value codec. Layout and style services depend on it; it does not depend on their class-specific policies.

## Invariants

- Decode and validation complete against temporary storage before a transaction changes the live object.
- Only editable direct properties accepted by an owning service can reach the codec.
- Arrays, sets, maps, delegates, interfaces, instanced references, and other unsafe reflected forms reject.
- Widget, slot, and Blueprint identities are re-resolved from the current asset for every request.
- Results use authoritative post-mutation inspection snapshots.

## Verification

The UMG Automation case exercises nested reflected Canvas layout plus scalar Text and ProgressBar values. Python schema tests bound recursive input depth and reject extra or malformed fields.
