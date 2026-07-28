# Layout, style, and binding contracts

All six operations extend `widget_tree_edit` and therefore require an exact 32-hex `operation_id`, mounted Widget Blueprint `asset_path`, and current 40-hex `expected_snapshot`.

- `set_slot`: `slot_id`, exact `property_name`, and recursive `value`.
- `set_style`: `widget_id`, exact `property_name`, and recursive `value`.
- `bind_property`: variable-exposed `widget_id`, widget `target_property`, `source_kind` of `property` or `function`, and exact `source_name`.
- `unbind_property`: `widget_id` and exact widget `target_property`.
- `bind_event`: variable-exposed `widget_id` and exact multicast `delegate_name`.
- `unbind_event`: `widget_id`, `delegate_name`, and exact `policy: "reject_if_connected"`.

Recursive values accept JSON scalar values, bounded arrays, tagged references, and tagged structs with bounded nested fields. Maximum depth is 4, struct/array size is 64, and text/path sizes use the shared Game Data codec limits. The native reflected property must also be live, editable, allowlisted, and codec-compatible; set/map properties remain unavailable.

Panel-slot records add `supported_layout_properties` and a `layout` object containing encoded values. Widget records add `supported_style_properties`. The `widget_bindings` inspection section emits `widget_property_binding` records with widget/property/source identities and `cost: "polling"`, plus `widget_event_binding` records with widget/event/graph/node identities and `cost: "event_driven"`.

The binding limit is 256 per Widget Blueprint. Property functions must be pure or const, take no input parameters, and return the widget delegate's compatible value. Event unbinding never cascades through graph links. Animations, screenshots, runtime widget instances, input mode, and gameplay-state changes are excluded.
