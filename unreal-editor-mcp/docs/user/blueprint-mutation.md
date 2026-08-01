# Blueprint mutation

## Reliable Actor Blueprint mutation

Call `capabilities` before mutation and retain its `bridge_instance_id`. Generate a fresh 32-character lowercase hexadecimal `operation_id` for every intended mutation. Reusing the same ID and exact request returns its retained result without executing again; reusing it with different arguments returns `operation_conflict`.

If a mutation times out or its response is lost, do not retry it with a new ID. Reconcile first:

```json
{
  "operation_id": "0123456789abcdef0123456789abcdef",
  "bridge_instance_id": "fedcba9876543210fedcba9876543210"
}
```

`queued` may be cancelled by adding `"cancel": true`; `executing` is not interrupted unsafely. `committed` contains the verified retained result, `partial` contains a non-retry-safe mutation result whose persistence or reload verification disagreed, and `rejected` contains the retained error. `outcome_unknown` means either an explicitly retained unknown result or that the bridge restarted/forgot the record: inspect the asset before deciding on another mutation.

## Creation, components, defaults, compile, and save

Create a Blueprint with one exact parent class and one destination long package name. Native parents use `/Script/Module.Class`; Blueprint parents use their generated class path ending in `_C`:

```json
{
  "operation_id": "11111111111111111111111111111111",
  "parent_class": "/Script/Engine.Actor",
  "package_path": "/Game/Actors/BP_Door"
}
```

The parent must be Actor-derived and usable as a Blueprint base. Missing, non-Actor, abstract, deprecated, skeleton, reinstanced, editor-only, compiling, and compile-error parents reject before package creation. The destination must not include an object suffix. Any loaded object, package, registry asset, or file already occupying the destination returns `already_exists`; the tool never generates an alternate name or overwrites content.

Creation compiles and saves before publishing the asset. A mandatory compile failure returns `compile_failed`; a save operation failure returns `save_failed`; a read-only file or unwritable destination returns `write_conflict`. Before publication, any failure removes only the new asset/file and releases its requested package namespace so the same destination can be retried. Existing assets are never deleted during failure cleanup.

Every existing-asset mutation requires the current `snapshot_id` from inspection or the preceding mutation result. For example, add a scene component and make it the root in two atomic calls:

```json
{
  "operation_id": "22222222222222222222222222222222",
  "asset_path": "/Game/Actors/BP_Door.BP_Door",
  "expected_snapshot": "0123456789abcdef0123456789abcdef01234567",
  "operation": "add",
  "component_class": "/Script/Engine.SceneComponent",
  "name": "SceneRoot"
}
```

The component result returns its stable ID and a new snapshot. Subsequent operations may `remove`, `rename`, `reparent`, `set_root`, or `set_property` by that exact ID. Only locally owned editable components are mutable. Adds accept suitable Blueprint-spawnable `UActorComponent` classes; names must be unique, scene attachments must be acyclic, non-scene components cannot be attached or rooted, and native/inherited components reject mutation.

Edit one generated-class default with `blueprint_default_edit`:

```json
{
  "operation_id": "33333333333333333333333333333333",
  "asset_path": "/Game/Actors/BP_Door.BP_Door",
  "expected_snapshot": "89abcdef0123456789abcdef0123456789abcdef",
  "property_name": "InitialLifeSpan",
  "value": 12.5
}
```

Property writes share inspection's codec and accept only safely editable reflected fields. Object/class references must be compatible visible packageable paths; delegates, interfaces, containers, transient/editor-only fields, arbitrary pointers, and non-finite numbers reject. Each edit is one editor transaction, so normal Unreal Undo/Redo applies. An unexpected failed postcondition is undone before an error is returned.

## Blueprint member variables

Inspect `variables` before every member edit and target existing members by their returned stable `id`. Add one integer member with a tagged default and validated metadata:

```json
{
  "operation_id": "44444444444444444444444444444444",
  "asset_path": "/Game/Actors/BP_Door.BP_Door",
  "expected_snapshot": "0123456789abcdef0123456789abcdef01234567",
  "operation": "add",
  "name": "Health",
  "type": {"category": "int", "container": "none"},
  "default": {"kind": "literal", "value": 100},
  "metadata": {
    "category": "Stats",
    "instance_editable": true,
    "blueprint_visible": true,
    "save_game": true,
    "replication": "replicated"
  }
}
```

Supported type categories are `boolean`, `byte`, `int`, `int64`, `real` (`float` or `double` subcategory), `name`, `string`, `text`, `enum`, `struct`, and hard/soft object/class references. Containers are `none`, `array`, `set`, and `map`; maps carry a scalar `value_type`. Defaults use explicit `engine_default`, `literal`, `reference`, `array`/`set`, or `map` forms, with at most 64 container entries. Arbitrary non-default struct serialization is not accepted.

`rename` preserves the member GUID. `update` changes exactly one of `type`, `default`, or `metadata`. A type update and every `remove` must include `"policy": "reject_if_referenced"`; referenced members return `referenced_member` without deleting nodes or changing the Blueprint. Inherited members are read-only. RepNotify metadata additionally requires one exact user-owned impure zero-parameter/zero-return function plus a live `ELifetimeCondition` name; inspection reports the related function identity and relationship validity.

## Blueprint functions and local variables

Inspect `functions`, `parameters`, and `local_variables` before editing. Only locally owned editable user-function graphs may be changed; inherited functions, parent overrides, and interface implementations remain inspectable but read-only. Add a complete function shell through the existing `blueprint_member_edit` tool:

```json
{
  "operation_id": "77777777777777777777777777777777",
  "asset_path": "/Game/Actors/BP_Door.BP_Door",
  "expected_snapshot": "0123456789abcdef0123456789abcdef01234567",
  "target": "function",
  "operation": "add",
  "name": "ComputeHealth",
  "signature": {
    "access": "protected",
    "pure": false,
    "const": true,
    "parameters": [
      {"name": "Delta", "direction": "input", "type": {"category": "int", "container": "none"}, "default": {"kind": "literal", "value": 0}},
      {"name": "Label", "direction": "input", "type": {"category": "string", "container": "none", "reference": true, "const": true}},
      {"name": "Result", "direction": "output", "type": {"category": "int", "container": "none"}}
    ]
  },
  "metadata": {"category": "Stats", "tooltip": "Computes a health value"}
}
```

The result returns a stable function ID. Function rename preserves that ID. A complete-signature update and removal require `"policy": "reject_if_referenced"`; existing call nodes cause `referenced_member` and are never deleted or repaired. Complete signatures accept at most 32 ordered parameters and validate access, pure/const flags, directions, types, reference/const qualifiers, and input defaults before committing. Function shells retain their required entry node and at least one result node.

Add a local to that exact function using its returned ID and the latest snapshot:

```json
{
  "operation_id": "88888888888888888888888888888888",
  "asset_path": "/Game/Actors/BP_Door.BP_Door",
  "expected_snapshot": "89abcdef0123456789abcdef0123456789abcdef",
  "target": "local_variable",
  "operation": "add",
  "function_id": "fedcba9876543210fedcba9876543210",
  "name": "WorkingValue",
  "type": {"category": "int", "container": "none"},
  "default": {"kind": "literal", "value": 0}
}
```

Local identity is a stable GUID scoped to its function. Rename preserves it; type changes and removal use the same reject-if-referenced policy. Each accepted edit is one Unreal transaction with exact read-back, so normal Undo/Redo applies.

## Blueprint macros and custom events

Inspect `macros`, `custom_events`, `parameters`, and `graphs` before editing. Macros use their graph GUID as `macro_id`; custom events use their node GUID as `custom_event_id` and report the containing event graph. Inherited macros, inherited custom events, custom-event overrides, native override events, interface functions, and ordinary functions remain separate records and are not retargeted by name.

Add a macro shell with a complete pure/impure signature:

```json
{
  "operation_id": "99999999999999999999999999999999",
  "asset_path": "/Game/Actors/BP_Door.BP_Door",
  "expected_snapshot": "0123456789abcdef0123456789abcdef01234567",
  "target": "macro",
  "operation": "add",
  "name": "ClampHealth",
  "signature": {
    "pure": true,
    "parameters": [
      {"name": "Value", "direction": "input", "type": {"category": "int", "container": "none"}, "default": {"kind": "literal", "value": 100}},
      {"name": "Result", "direction": "output", "type": {"category": "int", "container": "none"}}
    ]
  },
  "metadata": {"category": "Stats", "tooltip": "Clamps one health value"}
}
```

Custom-event creation additionally requires the stable `graph_id` of one compatible local event graph. Its signature contains input parameters only:

```json
{
  "operation_id": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
  "asset_path": "/Game/Actors/BP_Door.BP_Door",
  "expected_snapshot": "89abcdef0123456789abcdef0123456789abcdef",
  "target": "custom_event",
  "operation": "add",
  "graph_id": "fedcba9876543210fedcba9876543210",
  "name": "OnHealthChanged",
  "signature": {
    "parameters": [
      {"name": "NewHealth", "type": {"category": "int", "container": "none"}, "default": {"kind": "literal", "value": 100}}
    ]
  },
  "metadata": {"category": "Stats", "tooltip": "Health changed", "call_in_editor": true}
}
```

Rename preserves the macro graph or custom-event node identity. Signature changes and removal require `reject_if_referenced`; the bridge never deletes or repairs macro instances or custom-event call nodes. Macro tunnel entry/exit nodes and custom-event graph placement are verified after every accepted mutation.
