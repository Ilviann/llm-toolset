# CommonUI Widget Blueprint inspection

## Ownership and boundaries

`plugin/UnrealMCPCommonUI/` owns every direct `CommonUI`, `CommonInput`, `UMG`, and `UMGEditor` dependency. Its editor module registers one read-only companion API v1 contribution for `UCommonUserWidget` class defaults. The base `UnrealMCP` plugin remains independent of CommonUI headers, modules, plugin enablement, content, and transitive dependencies.

The initial frozen allowlist contains only Blueprint assets generated from `UCommonUserWidget` descendants. `UCommonActivatableWidget` descendants receive the activation and reference records in addition to the common widget record. CommonUI Data Assets, style Blueprints, settings, arbitrary referenced assets, runtime widgets, input simulation, and activation/navigation execution are excluded.

## Inspection flow

The base companion registry admits the module only when the project effectively enables the Engine `CommonUI` plugin, the `CommonUI` and `CommonInput` modules are loaded, descriptor and compiled identity agree, and API/schema versions exactly match. Python then intersects the native contribution with the fixed `unreal-mcp-commonui` catalog before adding `commonui_widget`, `commonui_activation`, and `commonui_references` to ordinary `blueprint_inspect` requests.

The base inspector retains the published `widget` family, Asset Registry discovery, ordinary Blueprint and Widget-tree records, pagination, Game-thread dispatch, dirtiness checks, and snapshot construction. It passes the verified generated-class default object to the companion, appends at most three typed records, and incorporates all 17 allowlisted properties into the same snapshot even when a typed section is not selected.

## Invariants and capability policy

Inspection reads exact named public-header properties only. Scalar records report `source: local` or `source: inherited`; hard and soft references report stable identities, paths, loaded resolution state, class paths when resolved, and ownership without loading the soft target. A non-activatable CommonUI widget returns explicit `not_activatable_widget` records rather than fabricating activation state.

Capabilities publish `commonui_widget_blueprints_inspection: true`, `commonui_widget_blueprints_mutation: false`, one `commonui_widget` contribution, its three typed sections, and effective record/property limits only while the companion is ready. The companion registers no mutation handler, never installs or enables CommonUI, and never compiles, saves, instantiates, executes, normalizes, or changes inspected assets.

## Verification

`UnrealMCP.CommonUI.WidgetBlueprintInspection` checks typed values, inheritance-safe property access, unresolved soft references, deterministic fingerprints, and package-dirtiness preservation. `UnrealMCP.CommonUI.WidgetBlueprintLiveFixture` creates the restart-persistent acceptance fixture. Python catalog/release tests cover exact admission and the absence of mutation schemas; the production-socket scenario checks ready capabilities, ordinary Widget-family preservation, all typed sections, unresolved reference state, and deterministic repeated read-back. Windows release verification also covers adaptive, forced-unity, non-unity, native Automation, base/companion packaging, and the full affected app suite.

[Wire contracts](../types/commonui-widget-inspection/index.md) · [User guide](../user/commonui-widget-blueprints.md) · [Architecture index](index.md)
