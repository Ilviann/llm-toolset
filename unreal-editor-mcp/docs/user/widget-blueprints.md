# Widget Blueprint authoring

Version 0.21.0 publishes `UUserWidget` descendants as the `widget` Blueprint family. They reuse Blueprint discovery, members, functions, locals, macros, custom events, action catalog, graph editing, defaults, compilation, saving, snapshots, and operation reconciliation. `blueprint_component_edit` is intentionally unavailable because Designer widgets are not Actor components.

Create with `blueprint_create` and a parent such as `/Script/UMG.UserWidget`. Widget creation uses Unreal's Widget Blueprint path, then performs the same mandatory initial compile/save and returns `blueprint_family: "widget"` with `family_capabilities.widget_tree: true`.

Inspect `widget_tree` for local widget and slot records. Use `widget_id` with `widget_defaults` and one-to-32 `property_names` for targeted default reads. Keep the returned stable widget/slot IDs and latest 40-character snapshot; re-inspect after compilation, undo/redo, reload, or any conflicting edit.

`widget_tree_edit` supports one stale-safe operation at a time: create the required root, add a child to a panel or empty named slot, remove an unreferenced non-root subtree, rename, reparent, toggle variable exposure, or set one supported direct widget property. Each request needs a fresh 32-hex operation ID and current snapshot. Panel targets use `{"kind":"panel","parent_id":"…","index":0}`; named slots use `{"kind":"named_slot","slot_id":"…"}`.

Compile and save explicitly after editing. The service preserves required roots, validates panel/named-slot rules and circular user-widget composition, and rejects destructive operations when graphs, delegate bindings, or animations still reference the affected subtree. Current limits are 512 widgets, depth 32, 256 named slots, 16 returned changed defaults per widget, and 1,024 changed defaults per tree. Runtime `capabilities.limits` is authoritative.

See [`../../examples/widget-tree-workflow.json`](../../examples/widget-tree-workflow.json) for a complete request sequence. Layout/style authoring, bindings, Widget-specific event wiring, and animations are reserved for the later `umg-authoring` feature.
