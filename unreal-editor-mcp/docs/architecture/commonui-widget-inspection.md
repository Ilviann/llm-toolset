# CommonUI Widget Blueprint inspection

## Ownership and boundaries

`plugin/UnrealMCPCommonUI/` owns every direct `CommonUI`, `CommonInput`, `UMG`, and `UMGEditor` dependency. Its editor module registers one read-only JSON-neutral companion API v2 inspection overlay for supported `UUserWidget` Widget Blueprints. The base `UnrealMCP` plugin remains independent of CommonUI headers, modules, plugin enablement, content, and transitive dependencies.

Root defaults remain a separate concern: `UCommonUserWidget` descendants receive the released common-widget record, and `UCommonActivatableWidget` descendants additionally receive activation and reference records. `UnrealMCPCommonUIWidgetTreeInspection` owns the effective-tree overlay for 21 frozen CommonUI widget families, exact property codecs, stable relationships, pages, and cumulative fingerprints. CommonUI Data Assets, style Blueprints, settings, arbitrary referenced assets, runtime widgets, input simulation, and activation/navigation execution are excluded.

## Inspection flow

The base companion registry admits the module only when the project effectively enables the Engine `CommonUI` plugin, the `CommonUI` and `CommonInput` modules are loaded, descriptor and compiled identity agree, and API/schema versions exactly match. The inspection-only family `commonui_widget` matches supported `UUserWidget` assets and routes four typed selectors through the common `asset_inspect` facade while the base retains identity, snapshots, limits, and read-only preservation.

`commonui_widget`, `commonui_activation`, and `commonui_references` remain root-default selectors. `commonui_widgets` is a pageable collection with stable nested widget-name details. The companion resolves the effective design-time `WidgetTree`, rejects more than 128 widgets, filters the frozen CommonUI types, sorts records deterministically, and encodes at most 48 exact properties per CommonUI widget. Root and tree fingerprints always join the same common snapshot, including fields omitted from a page or selector.

## Invariants and capability policy

Inspection reads exact named public-header properties only. Widget records report stable identity, exact class and semantic family, local/inherited ownership, parent identity, and ordered child identities. Values report local/inherited source and a bounded type; hard, class, weak, and soft references report stable identities, paths, loaded resolution state, and class paths without loading soft targets. Data Table row handles retain table identity and row name. A non-activatable CommonUI root returns explicit `not_activatable_widget` records rather than fabricating activation state.

Capabilities publish one `commonui_widget` family against `/Script/UMG.UserWidget`, its four selector routes, one stable nested identity kind, and effective record/property/widget/input-action limits only while the companion is ready. The companion registers no mutation handler, never installs or enables CommonUI, and never compiles, saves, instantiates, executes, normalizes, or changes inspected assets.

## Verification

`UnrealMCP.CommonUI.WidgetBlueprintInspection` checks all 21 frozen class/property contracts, CommonUI and ordinary roots, collection paging, typed values, inheritance-safe property access, unresolved soft references, deterministic fingerprints, and package-dirtiness preservation. `UnrealMCP.CommonUI.WidgetBlueprintLiveFixture` creates the restart-persistent acceptance fixture with a Common Text child. Python catalog/release tests cover exact admission and the absence of mutation schemas; the production-socket scenario checks ready capabilities, ordinary Widget-family preservation, root/page/detail selectors, and deterministic repeated read-back. Windows release verification also covers adaptive, forced-unity, non-unity, native Automation, base/companion packaging, and the full affected app suite.

[Wire contracts](../types/commonui-widget-inspection/index.md) · [User guide](../user/commonui-widget-blueprints.md) · [Architecture index](index.md)
