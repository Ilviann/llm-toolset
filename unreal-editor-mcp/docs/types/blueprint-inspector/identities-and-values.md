# Identities and value encodings

The inspector uses Unreal-provided persistent GUIDs where available:

- SCS component `VariableGuid`
- member variable `VarGuid`
- graph `GraphGuid`
- node `NodeGuid`
- pin `PinId`

Each identity-bearing record contains `id` and `identity_stable`. If Unreal provides no valid GUID, `id` is empty and `identity_stable` is false; the inspector does not invent a mutable-object identity. Connection records reference graph, node, and pin IDs. Component records publish `ownership` (`local`, `inherited`, or `native`), owning class/Blueprint, `editable`, `scene_component`, and `root`; native and inherited components remain read-only.

Snapshots hash sorted structural identity, ownership, names, graph/node position, K2 types and defaults, component hierarchy and changed defaults, and pin links. The representative behavioral fixture retains its snapshot through undo, compile/node reconstruction, save, editor restart, and reload. Callers must still discard prior cursors and re-inspect after any compile, undo/redo, reload, or reconstruction because Unreal may replace nodes or pins in other Blueprints; a changed snapshot is an explicit conflict, not a retargeting signal.

K2 types contain `category`, `subcategory`, `container`, `reference`, `const`, `supported`, and optional `type_object`. Supported common categories are execution, Boolean, byte, integer, real-number variants, name, string, text, enum, struct, object/class/soft references, interface, and wildcard. Unknown categories remain visible with `supported: false` rather than being reflected recursively.

Member-variable defaults use the tagged canonical K2 forms documented with the mutator and are reconstructed from the generated-class CDO after compile. Pin defaults remain bounded strings, with a bounded object path for a pin default object. Component and `class_default` records encode only requested properties or bounded changed defaults. Boolean, finite numeric, name/string/text, enum/flags, common math/color/transform structs, and compatible hard/soft object/class references use `{name,supported:true,type,value}`. Structs use Unreal's canonical bounded text form; references use visible packageable object/class paths. Other reflected types use `{name,supported:false,type:"unsupported"}`. Delegates, interfaces, transient/editor-only fields, and arbitrary UObject graphs are never writable or recursively serialized.

Variable records additionally expose category/tooltip and supported metadata flags, local/inherited ownership, editability, replication mode/condition, RepNotify function identity, and a bounded reference summary. The summary lists at most 64 loaded graph/node relationships and separately flags references that Unreal reports but cannot identify in the loaded graph set.
