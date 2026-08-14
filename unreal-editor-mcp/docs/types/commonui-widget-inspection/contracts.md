# CommonUI Widget inspection contracts

## Family, records, and references

An admitted `unreal-mcp-commonui` schema-revision-2 companion publishes inspection-only family `commonui_widget` for Blueprint-generated `UCommonUserWidget` descendants. The common result retains neutral Widget Blueprint identity while the companion adds its exact blocks and selectors.

The typed request sections add at most three records:

- `commonui_widget` contains `bDisplayInActionBar` and `bConsumePointerInput` defaults.
- `commonui_activation` contains supported-state plus back handling/display, display text, auto activation, activation focus, modal/focus restoration, action-domain override selection, input mapping priority, and activated/deactivated visibility configuration for `UCommonActivatableWidget` descendants.
- `commonui_references` contains the hard `InputMapping` and soft `ActionDomainOverride` references. Each record has a deterministic 32-character identity, object path, loaded `resolved` state, class path when resolved, and local/inherited source. Inspection never loads an unresolved soft target.

Every exposed scalar reports `value` and `source`. For non-activatable descendants, activation and reference records report `supported: false` and `unavailable_reason: not_activatable_widget`. All allowlisted property text joins ordinary Blueprint fingerprint material, so any exposed CommonUI state change invalidates cursor continuation through `stale_precondition`, including state omitted from a selected page.

## Capabilities, bounds, and exclusions

The family is ready only when `UnrealMCPCommonUI`, Engine `CommonUI`, loaded `CommonUI` and `CommonInput` modules, companion API v2, schema revision 2, and the matching Python catalog agree. Native capabilities publish semantic version 0.2.0, inspection true, creation/editing false, three selector routes, three maximum records, and 17 exact inspected properties.

The base inspector still enforces its 4,096-record structural ceiling, 100-record page ceiling, 32 retained cursors, 30-second cursor lifetime, five-second command deadline, and 256-KiB response ceiling. The companion adds no discovery scan or retained state and traverses no dependency graph.

Common Button/Text Style Blueprints, CommonUI Rich Text data, input-domain Data Assets, project settings, arbitrary referenced assets, non-CommonUI Widget Blueprints, runtime widget instances, activation/navigation, input-device simulation, creation, compilation, saving, and CommonUI-owned mutation are outside this contract.
