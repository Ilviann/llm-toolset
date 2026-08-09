---
feature_id: commonui-assets-inspect
status: completed
depends_on:
  - umg-authoring
  - companion-plugins
released_in: "0.35.0"
---

# `commonui-assets-inspect` — Inspect assets with CommonUI plugin dependencies

**Outcome:** Agents can discover and inspect supported CommonUI Widget Blueprints through the optional independently versioned `UnrealMCPCommonUI` companion without changing an asset or adding CommonUI dependencies to the base plugin.

**Implementation status:** Completed in 0.35.0 with `UnrealMCPCommonUI` 0.1.0 on unchanged companion API v1 and schema revision 1. The frozen initial allowlist is `UCommonUserWidget`-derived Widget Blueprints, including typed `UCommonActivatableWidget` defaults. Mandatory Windows verification covers Python/static contracts, adaptive/forced-unity/non-unity builds, native Automation, production-socket headless integration, and separate base/CommonUI packaging. macOS remains preferred non-blocking follow-up work; Linux is out of scope.

**Depends on:**

- [`umg-authoring`](umg-authoring.md)
- [`companion-plugins`](companion-plugins.md)

### Project enablement and companion contract

- `UnrealMCPCommonUI` owns all direct `CommonUI`, `CommonInput`, `UMG`, and `UMGEditor` dependencies. The base plugin remains CommonUI-free and retains its complete non-CommonUI contract when the companion or Engine plugin is absent, disabled, unloaded, stale, or incompatible.
- Admission requires effective project enablement for Engine `CommonUI`, loaded required modules, exact descriptor/compiled semantic and API/schema identity, owner-matched registration, and Python catalog intersection. The companion never installs/enables CommonUI, edits a project descriptor, or restarts the editor.
- Capabilities publish companion identity, dependency state, `commonui_widget_blueprints_inspection: true`, mutation false, three typed sections, the exact target class, and stable limits only while the complete state is ready.

### Inspection implementation

- Ordinary `blueprint_inspect` discovery and results retain the `widget` family. The companion augments exact generated-class defaults with `commonui_widget`, `commonui_activation`, and `commonui_references` while preserving ordinary Blueprint, Widget-tree, default, binding, and graph records.
- Records cover CommonUI input display/consumption, activatable back/focus/modal/visibility/mapping defaults, and hard/soft mapping/action-domain references. Values report local/inherited ownership; references have stable identities, paths, loaded resolution state, and class paths without loading soft targets.
- Non-activatable CommonUI widgets expose stable unsupported reasons. Every one of the 17 allowlisted properties joins the ordinary fingerprint and one authoritative snapshot regardless of selected sections.
- Inspection is bounded to three companion records and performs no dependency traversal, runtime execution, instantiation, compilation, saving, normalization, package mutation, or Undo mutation.

### Verification and documentation

- Focused native Automation covers activatable and non-activatable layouts, exact property probes, unresolved references, deterministic fingerprints, package-dirtiness preservation, and a saved restart fixture.
- Python catalog, release, packaging, and headless contracts cover absent/not-ready/mismatched states, exact read-only admission, capability composition, ordinary Widget-family preservation, deterministic read-back, and base dependency isolation.
- Windows adaptive, forced-unity, non-unity, native Automation, full Python suite, base/CommonUI packaging, and production-socket integration pass before release completion. The [user guide](../../user/commonui-widget-blueprints.md), [architecture component](../../architecture/commonui-widget-inspection.md), and [wire contracts](../../types/commonui-widget-inspection/index.md) define the released allowlist, records, bounds, and exclusions.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
