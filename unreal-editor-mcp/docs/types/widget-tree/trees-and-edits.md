# Widget trees and edits

The internal Widget Blueprint collector retains `widget_tree` and targeted `widget_defaults` records used by native authoring and preservation tests. The released `asset-inspect-umg` adapter projects a separate model-facing semantic view and does not expose edit-oriented IDs. An internal `widget` record contains stable `id`, name, class path, ownership/editability, root and variable flags, parent/slot identities, child index, supported style properties, changed defaults, and truncation state. A panel `widget_slot` adds supported layout properties and encoded layout values; named slots identify host, slot name, child, and inherited/tree-host state.

Widget IDs are 32 lowercase hexadecimal characters backed by `WidgetVariableNameToGuidMap`, including non-variable widgets. Panel-slot IDs hash the stable parent/child pair. Named-slot IDs hash the stable host plus exact slot name. The 40-character Blueprint snapshot fingerprints names, classes, hierarchy, slot relationships, variable flags, changed defaults, layout values, and property/event bindings.

`widget_tree_edit` always requires `operation_id`, `asset_path`, `expected_snapshot`, and one operation:

- `set_root`, `add`, `remove`, `rename`, `reparent`, and `set_variable` own hierarchy and exposure.
- `set_property` edits one legacy supported direct widget default.
- `set_slot`, `set_style`, `bind_property`, `unbind_property`, `bind_event`, and `unbind_event` use the UMG authoring contracts.

Success returns family/capability data, operation, stable affected identities, new snapshot, and dirty state. Edits transact and mark the Blueprint appropriately but never compile or save. Root destruction/reparenting, circular composition, incompatible containers, occupied named slots, nonlocal identities, unsafe classes/properties, destructive references, and stale snapshots reject with stable errors.

Tree limits are 512 widgets, depth 32, 256 named slots, 16 visible changed defaults per widget, 1,024 changed defaults per tree, 128-character names/properties, and shared request/response/ledger bounds. See [`../../types/umg-authoring/`](../umg-authoring/index.md) for layout, style, and binding details. Animations and arbitrary Slate/C++ authoring remain excluded.
