# CommonUI Widget inspection contracts

## Family, records, and references

An admitted `unreal-mcp-commonui` schema-revision-2 companion publishes inspection-only family `commonui_widget` as a composable overlay for supported Blueprint-generated `UUserWidget` descendants. The common result retains neutral Widget Blueprint identity and base UMG records while the companion adds its exact blocks and selectors.

The three released root-default sections remain unchanged for `UCommonUserWidget` roots:

- `commonui_widget` contains `bDisplayInActionBar` and `bConsumePointerInput` defaults.
- `commonui_activation` contains supported-state plus back handling/display, display text, auto activation, activation focus, modal/focus restoration, action-domain override selection, input mapping priority, and activated/deactivated visibility configuration for `UCommonActivatableWidget` descendants.
- `commonui_references` contains the hard `InputMapping` and soft `ActionDomainOverride` references. Each record has a deterministic 32-character identity, object path, loaded `resolved` state, class path when resolved, and local/inherited source. Inspection never loads an unresolved soft target.

Every exposed root scalar reports `value` and `source`. For non-activatable descendants, activation and reference records report `supported: false` and `unavailable_reason: not_activatable_widget`.

Every supported Widget Blueprint additionally exposes `commonui_widgets` as a pageable collection descriptor. `commonui_widgets/<widget-name>` selects one exact detail record. Each record contains a deterministic 32-character `widget_id`, name, exact class path, one of 21 frozen semantic families, local/inherited ownership, parent ID/name, direct child IDs, and an exact property map. Properties encode booleans, numbers, enums, names, bounded text/exports, Data Table rows, and hard/class/weak/soft references; unresolved soft targets are never loaded.

The frozen families cover Common User/Activatable Widget, Button, Text, Rich Text, Numeric Text, Date/Time Text, Action Widget, Lazy Image/Widget, Animated and Activatable Switcher, Activatable Stack/Queue/Container, Tab List, List/Tile/Tree View, and Carousel/Carousel Navigation. All root and tree property material participates in one query-independent snapshot, so a selected or omitted CommonUI value change invalidates stale continuation.

## Capabilities, bounds, and exclusions

The family is ready only when `UnrealMCPCommonUI`, Engine `CommonUI`, loaded `CommonUI` and `CommonInput` modules, companion API v2, schema revision 2, and the matching Python catalog agree. Native capabilities publish semantic version 0.3.0, inspection true, creation/editing false, `/Script/UMG.UserWidget` with exact-and-derived policy, four selector routes, and the fixed record/property/widget/input-action limits.

The base inspector still enforces its 4,096-record structural ceiling, 100-record page ceiling, 32 retained cursors, 30-second cursor lifetime, five-second command deadline, and 256-KiB response ceiling. The companion additionally caps the effective tree at 128 widgets, each CommonUI record at 48 properties, and each input-action row array at 32 entries. It adds no discovery scan or retained state and traverses no dependency graph.

Direct inspection of Common Button/Text Style Blueprints, CommonUI Rich Text data, input-domain Data Assets, project settings, or arbitrary referenced assets remains outside this contract. Ordinary Widget Blueprints are supported only as containers for allowlisted CommonUI tree widgets. Runtime widget instances and runtime activation, focus, selection, navigation, lazy-load state, input simulation, creation, compilation, saving, and CommonUI-owned mutation are excluded.
