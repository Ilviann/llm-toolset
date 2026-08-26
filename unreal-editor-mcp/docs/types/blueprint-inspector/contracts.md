# Blueprint inspector contracts

Use the index to retrieve only the contract section relevant to the task.

## Queries, records, and pages

`blueprint_inspect` accepts exactly one of these argument families:

- Discovery: `mode: "discover"`, optional normalized mounted-content `package_path`, optional exact `asset_name`, and optional `page_size`.
- Inspection: `mode: "inspect"`, one exact mounted-content `asset_path`, optional `sections`, optional exact 32-character `graph_id`, `component_id`, `member_id`, `function_id`, `local_id`, `macro_id`, `custom_event_id`, or `widget_id`, optional exact `component_name`, optional one-to-32 exact `property_names`, optional `include_inherited`, and optional `page_size`. `component_id` and `component_name` are mutually exclusive.
- Continuation: one 32-character opaque `cursor` and optional `page_size`.

Omitting `package_path` searches all Asset Registry content mounts visible to the project. Supplying `/Game`, `/Engine`, `/PluginMount`, or a deeper package path narrows the scan. Inspection accepts package or object paths and normalizes to the object path. Raw filesystem paths, traversal, and backslashes reject. Defaults are `summary`, `parent_class`, `compile_state`, `components`, `variables`, `functions`, `macros`, `custom_events`, `local_variables`, `graphs`, and `widget_tree` where applicable. Explicit sections may additionally select `class_defaults`, `parameters`, `nodes`, `pins`, `connections`, targeted `widget_defaults`, and `widget_bindings`. When the exact GAS companion is ready, `gameplay_ability` is also available and is included by default for that family. Exact identity filters restrict corresponding output and scope; widget defaults require both `widget_id` and `property_names`.

Every success contains `mode`, a 40-character `snapshot_id`, `records`, `record_count`, `page_offset`, `scan_truncated`, and `has_more`. Discovery asset records add `blueprint_family` and `native_family_class`. Every exact-inspection page adds `blueprint_family` and live `family_capabilities`, including cursor continuations. A partial page also contains `next_cursor` and `cursor_expires_in_ms`. Records carry a `section` discriminator including `variable`, `function`, `macro`, `custom_event`, `parameter`, `local_variable`, graph structure, defaults, and summary records. Editable declaration records include everything required to preflight the corresponding accepted `blueprint_member_edit` request.

Discovery records are sorted by exact object path. The scan stops after 2,048 Blueprint registry candidates and reports `scan_truncated`; it does not load those assets. Deep inspection rejects more than 4,096 structural fingerprint records. Pages default to 25 and accept at most 100 records.

A cursor is single-use, expires after 30 seconds, and occupies one of 32 retained slots. It stores the normalized initial query, structural snapshot, and next offset. Continuation recomputes the query and returns `stale_precondition` if the snapshot changed. An expired, consumed, evicted, or unknown cursor returns `cursor_expired`.

## Identities and value encodings

The inspector uses Unreal-provided persistent GUIDs where available:

- SCS component `VariableGuid`
- Widget Blueprint `WidgetVariableNameToGuidMap` entry
- member variable `VarGuid`
- graph `GraphGuid`
- user function `GraphGuid`
- macro graph `GraphGuid`
- function-local variable `VarGuid`
- node `NodeGuid`
- custom-event node `NodeGuid`
- pin `PinId`

Each identity-bearing record contains `id` and `identity_stable`. If Unreal provides no valid GUID, `id` is empty and `identity_stable` is false; the inspector does not invent a mutable-object identity. Connection records reference graph, node, and pin IDs. Component records publish `ownership` (`local`, `inherited`, or `native`), owning class/Blueprint, `editable`, `scene_component`, and `root`; native and inherited components remain read-only. A child Blueprint can target an inherited SCS component with its ancestor-stable `component_id` or exact `component_name` even when `include_inherited` is false. Widget records publish local hierarchy and variable/default state; widget slots use deterministic IDs derived from stable owners, children, and named-slot names.

Snapshots hash sorted structural identity, ownership, names, graph/node position, K2 types and all string/object/text default storage, component hierarchy and changed defaults, and pin links. The representative behavioral fixture retains its snapshot through undo, compile/node reconstruction, save, editor restart, and reload. Callers must still discard prior cursors and re-inspect after any compile, undo/redo, reload, or reconstruction because Unreal may replace nodes or pins in other Blueprints; a changed snapshot is an explicit conflict, not a retargeting signal.

One UE 5.7 editor-derived state is not a structural pin: a `K2Node_PromotableOperator` may retain a hidden `ErrorTolerance` input with no type, links, or string/object/text default and regenerate only its GUID on first reload. Inspection omits exactly that state. A tolerance pin that is typed, visible, linked, or default-bearing remains an ordinary identity-bearing structural pin.

K2 types contain `category`, `subcategory`, `container`, `reference`, `const`, `supported`, and optional `type_object`. Supported common categories are execution, Boolean, byte, integer, real-number variants, name, string, text, enum, struct, object/class/soft references, interface, and wildcard. Unknown categories remain visible with `supported: false` rather than being reflected recursively.

Member-variable defaults use the tagged canonical K2 forms documented with the mutator and are reconstructed from the generated-class CDO after compile. Pin records keep bounded raw `default_value`, optional `default_object`/`default_text`, and a tagged `default` reconstructed through the shared K2 codec; unavailable types remain explicit. A scalar `FGameplayAttribute` default uses `{kind:"gameplay_attribute",resolved,compatible,name,property_path,owner_path}`; the same object is the `value` of a supported targeted reflected property whose `type` is `gameplay_attribute`. Resolution is live and compatibility accepts floating-point or `FGameplayAttributeData`-derived fields. This is inspection-only and does not add Gameplay Attribute mutation.

Component and `class_default` records encode only requested properties or bounded changed defaults. Boolean, finite numeric, name/string/text, enum/flags, gameplay tag/container, GUID, common math/color/transform structs, compatible hard/soft object/class references, arrays, and bounded nested reflected structs use `{name,supported:true,type,value}`. Gameplay tags and GUIDs are strings, containers and arrays are arrays, references are visible packageable object/class paths, and general structs are `{kind:"struct",fields:{…}}`. An exact reflected `FGameplayModifierInfo` array uses this same form and admits the two additional internal levels needed for magnitude curves and tag queries without changing the published general depth limit. Other reflected types use `{name,supported:false,type:"unsupported"}`. Delegates, interfaces, transient/editor-only fields, and arbitrary UObject graphs are never writable or recursively serialized.

An inherited SCS component record is encoded from the effective template resolved for the inspected child class. `template_value_origin` is `local`, `local_override`, or `inherited`, and `template_source_blueprint` identifies the Blueprint supplying that template. Each requested or changed property additionally carries `value_origin`, `source_blueprint`, and `source_class`; the source class is used when the value comes from a native archetype rather than a Blueprint declaration or override.

Variable records additionally expose category/tooltip and supported metadata flags, local/inherited ownership, editability, replication mode/condition, RepNotify function identity, relationship validity, and a bounded reference summary. The summary lists at most 64 loaded graph/node relationships and separately flags references that Unreal reports but cannot identify in the loaded graph set.

Function records use the function graph GUID, distinguish user-owned functions from inherited, override, and interface functions, and report editability, complete signature, metadata, required entry/result nodes, RepNotify relationships, and bounded call references. Macro records use their graph GUID and expose pure/impure signatures, editable tunnel identities, graph relationships, metadata, and macro-instance references. Custom-event records use the event-node GUID, distinguish local, inherited, and custom-event override ownership, and report the exact containing event graph. Separate parameter records preserve callable owner kind/identity, order, direction, type, reference/const qualifiers, and tagged defaults. Local-variable records use their `VarGuid`, carry the owning function ID/name, and expose canonical type/default data plus scope-aware reference summaries.

## Normalized inspection query and collectors

`FInspectionQuery` is the validated internal form of one initial inspection request. It contains the canonical object path, inherited-content flag, deduplicated section set, stable identity or exact component-name filters, and targeted property-name set. Cursor requests remain owned and validated by the inspector facade and replay the retained normalized arguments against an expected snapshot.

The builder resolves and loads only the requested asset, captures dirty/compile state once, and passes one record array plus one fingerprint array through overview/component/default, member, function/local, macro, custom-event, graph, and admitted companion-family collectors. Each collector owns its family encoding and exact not-found behavior. The builder enforces the shared structural bound, verifies non-mutation, and hashes the complete base-plus-companion fingerprint once after all requested sections have observed the same structure.
