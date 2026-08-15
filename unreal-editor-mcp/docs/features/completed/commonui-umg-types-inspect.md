---
feature_id: commonui-umg-types-inspect
status: completed
depends_on:
  - asset-inspect-umg
  - commonui-assets-inspect
  - companion-asset-adapters
released_in: "0.49.0"
---

# `commonui-umg-types-inspect` — CommonUI types in UMG inspection

**Outcome:** `asset_inspect` reports bounded CommonUI-specific widget and value semantics wherever supported CommonUI types occur in a Widget Blueprint, without requiring the Widget Blueprint root class to derive from `UCommonUserWidget`.

**Implementation status:** Completed in 0.49.0 with `UnrealMCPCommonUI` 0.3.0 on unchanged companion API v2 and schema revision 2. The companion family is now a composable `UUserWidget` overlay; base UMG inspection remains available when CommonUI is absent or rejected.

**Depends on:**

- [`asset-inspect-umg`](asset-inspect-umg.md)
- [`commonui-assets-inspect`](commonui-assets-inspect.md)
- [`companion-asset-adapters`](companion-asset-adapters.md)

### Released inspection contract

- Root `UCommonUserWidget` assets retain the released `commonui_widget`, `commonui_activation`, and `commonui_references` records. Every supported Widget Blueprint additionally exposes the pageable `commonui_widgets` collection and stable `commonui_widgets/<widget-name>` detail selector while the companion is ready.
- The frozen 21-family allowlist covers Common User/Activatable Widgets, Common Button, Text, Rich Text, Numeric Text, Date/Time Text, Action Widget, Lazy Image/Widget, Animated and Activatable Switchers, Activatable Stack/Queue/Container, Tab List, List/Tile/Tree Views, and Carousel/Carousel Navigation widgets.
- Records provide stable widget IDs, exact class paths, local/inherited ownership, parent and child identities, and exact allowlisted properties. Values use bounded booleans, numbers, enums, names, text, exported structs, Data Table row handles, hard/class references, and soft references without loading unresolved targets.
- Safety limits are 128 effective tree widgets, 48 properties per CommonUI widget, 32 input-action rows per inspected array, 100 records per page through the base facade, and the existing document/value/deadline/snapshot ceilings. Exceeding a structural or input-action bound fails closed; page metadata reports further records without truncating a record.
- Direct recursive inspection of referenced style/data assets, runtime activation/focus/selection state, input-device simulation, dependency execution, project settings, mutation, compilation, saving, and plugin installation or enablement remain excluded.

### Verification

Frozen-class/property validation covers all 21 categories. Native behavioral coverage includes CommonUI roots, an ordinary `UUserWidget` root with a Common Text child, collection paging, nested selection, snapshots, unresolved references, and package-dirtiness preservation. The production-socket fixture persists a CommonUI child and verifies root, page, detail, and repeat-read behavior after restart. macOS follow-up passed on 2026-08-15 through the full native and production-socket gates, all three editor build modes, and isolated universal CommonUI packaging.

[Back to roadmap](../../../ROADMAP.md) · [Wire contracts](../../types/commonui-widget-inspection/index.md) · [User guide](../../user/commonui-widget-blueprints.md)
