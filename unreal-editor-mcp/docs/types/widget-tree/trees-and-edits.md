# Widget trees and edits

`blueprint_inspect` accepts `widget_tree` and targeted `widget_defaults` sections for the `widget` family. A `widget` record contains stable `id`, name, class path, local ownership/editability, root and variable flags, parent/slot identities, child index, changed-default count, up to 16 encoded changed defaults, and truncation state. `widget_slot` records distinguish `panel` from `named_slot`; panel slots identify parent, child, index, and slot class, while named slots identify host, slot name, child, and inherited/tree-host state. Targeted defaults require one exact `widget_id` plus one-to-32 `property_names` and return `widget_default` records through the bounded shared property codec.

Widget IDs are 32 lowercase hexadecimal characters backed by `WidgetVariableNameToGuidMap`, including non-variable widgets. Panel-slot IDs hash the stable parent/child pair. Named-slot IDs hash the stable host plus exact slot name. The 40-character Blueprint snapshot fingerprints names, classes, hierarchy, slot relationships, variable flags, and all changed-default values, including supported and unsupported properties.

`widget_tree_edit` always requires `operation_id`, `asset_path`, `expected_snapshot`, and one operation:

- `set_root`: exact live `widget_class` and local `name`; only when the tree has no root.
- `add`: class/name plus `target`, either a panel `parent_id` with optional insertion `index`, or one empty named `slot_id`.
- `remove`: stable `widget_id` and exact `policy: "reject_if_referenced"`.
- `rename`: stable ID and exact `new_name`.
- `reparent`: stable ID plus a panel or named-slot target.
- `set_variable`: stable ID and Boolean `is_variable`.
- `set_property`: stable ID, direct `property_name`, and one shared scalar/reference/struct value.

Success returns family/capability data, operation, stable `widget_id`, new snapshot, and dirty state; property edits also return `changed_property`, while removal returns `removed: true`. Edits transact and mark the Blueprint appropriately but never compile or save. Root destruction/reparenting, circular user-widget composition, incompatible containers, occupied named slots, nonlocal identities, unsafe classes/properties, destructive graph/delegate/animation references, and stale snapshots reject with stable errors.

Limits are 512 widgets, depth 32, 256 named slots, 16 visible changed defaults per widget, 1,024 changed defaults per tree, 128-character names/properties, and the shared request/response/ledger bounds. Responsive layout, styles, bindings, event wiring, animations, and arbitrary Slate/C++ authoring remain outside this feature.
