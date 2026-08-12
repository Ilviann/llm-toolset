# Widget Blueprint authoring

Version 0.22.0 publishes practical UMG authoring on the `widget` Blueprint family. Widget Blueprints reuse discovery, members, functions, custom events, action discovery, graph editing, compilation, saving, snapshots, and reconciliation. `blueprint_component_edit` remains unavailable because Designer widgets are not Actor components.

Create with `blueprint_create` and a parent such as `/Script/UMG.UserWidget`. Inspect `widget_tree` for stable widget and slot identities, supported style/layout properties, and encoded layout read-back. Inspect `widget_bindings` for property and Designer-event records. Keep the latest 40-character snapshot and re-inspect after compilation, undo/redo, reload, or conflicting edits.

`widget_tree_edit` accepts one stale-safe operation at a time:

- Hierarchy: `set_root`, `add`, `remove`, `rename`, `reparent`, and `set_variable`.
- Defaults and presentation: `set_property` and `set_style`.
- Slot layout: `set_slot`.
- Attribute bindings: `bind_property` and `unbind_property`.
- Designer events: `bind_event` and `unbind_event`.

Every request needs a fresh 32-hex operation ID, exact asset path, and current snapshot. `set_slot` targets a stable panel `slot_id`; ordering remains the child index selected by `add` or `reparent`. Supported properties are reported live. Common slot coverage includes Canvas anchors/offsets/alignment/auto-size/Z order; Grid row, column, spans, layer, nudge, padding, and alignment; Uniform Grid row/column/alignment; linear, scroll, overlay, Size Box, and Wrap Box padding/alignment/fill settings.

`set_style` is a presentation allowlist rather than arbitrary UObject mutation. It covers common visibility, enabled, transform, opacity, cursor, and clipping properties plus live class-specific properties for text, images, buttons, progress bars, borders, check boxes, sliders, spin boxes, combo boxes, and editable text widgets. Complex values use the bounded recursive form, for example `{"kind":"struct","fields":{"Minimum":{"kind":"struct","fields":{"X":0.0,"Y":0.0}}}}`; allowlisted arrays such as Combo Box `DefaultOptions` use ordinary bounded JSON arrays. Object/class references use the shared tagged reference form. Unsupported, runtime-only, delegate, instanced, set/map, and unsafe properties reject before mutation.

Property bindings require a variable-exposed target widget. `bind_property` selects one bindable widget delegate with `target_property` and one compatible Blueprint member property or pure/const zero-argument function with `source_kind` and exact `source_name`. Inspection reports `cost: "polling"` because UMG evaluates attribute bindings repeatedly. Prefer event-driven updates when practical. `unbind_property` removes only that exact widget/property binding.

`bind_event` creates the context-valid Designer event node for one exact `delegate_name`, returning stable graph and node IDs. Continue its UI logic with the ordinary action catalog and `blueprint_graph_edit`. `unbind_event` requires `policy: "reject_if_connected"` and refuses to remove a node that still has graph connections, preserving unrelated graph state.

The existing live action catalog remains authoritative for Widget Blueprint construction/destruction/focus/input callbacks, callable widget functions, and variable get/set nodes. Query it against the returned event graph, use its opaque context-valid action IDs, and connect only through `blueprint_graph_edit`; UMG authoring does not synthesize arbitrary calls or replacement graphs.

Compile and save explicitly. Limits are 512 widgets, tree depth 32, 256 named slots, 256 widget bindings, recursive value depth 4, 64 struct fields or collection entries, and the shared request/response/ledger bounds. Animations, visual screenshots, raw Slate/C++, runtime instance mutation, input-mode setup, and gameplay-state mutation remain outside this feature.

See [`../../examples/widget-tree-workflow.json`](../../examples/widget-tree-workflow.json) for basic hierarchy creation. Deep UMG read-back is deferred to `asset-inspect-umg`; core `asset_inspect` currently returns bounded identity and limitations for Widget Blueprints.
