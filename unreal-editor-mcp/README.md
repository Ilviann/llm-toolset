# Unreal Editor MCP

Unreal Editor MCP 0.14.0 is an offline-first MCP bridge for Unreal Engine 5.8+. It pairs a dependency-free Python 3.10+ stdio server with an editor-only C++ plugin. This release exposes exactly twelve tools:

- `capabilities` reports the exact Python/plugin/Unreal versions, commands, features, listener state, effective limits, and published Blueprint-family matrix.
- `editor_state` reports project identity, bridge readiness, play/simulate/save/GC state, and concise queued-operation state.
- `operation_status` reconciles or safely cancels one retained mutation by operation and bridge identity.
- `blueprint_inspect` discovers every published Blueprint family across mounted content and returns bounded pages of one selected Blueprint's structure.
- `blueprint_action_catalog` discovers bounded context-valid function, variable, event, flow-control, cast, literal, and operator actions for one exact Blueprint graph snapshot.
- `blueprint_graph_edit` creates, moves, removes, configures, or connects graph nodes and pins, including wildcard specialization and explicitly requested bounded conversions.
- `blueprint_create` creates, compiles, saves, and verifies one new supported Blueprint family without overwriting content.
- `blueprint_compile` explicitly compiles one mutable supported-family Blueprint and returns bounded diagnostics.
- `blueprint_save` explicitly saves one mutable supported-family Blueprint package without interactive dialogs.
- `blueprint_component_edit` adds, removes, renames, reparents, roots, or edits one local Actor component.
- `blueprint_default_edit` edits one supported Blueprint-generated class-default property.
- `blueprint_member_edit` adds, renames, updates, or safely removes one typed Blueprint variable, function, local variable, macro, or custom-event shell.

Phase 15 adds UObject-based GameInstance Blueprints to the same family-aware workflow. GameInstance supports defaults, members, callbacks, graph authoring, compile, and save, while component operations are explicitly unavailable. Every exact operation reports its family; capabilities publish the family/operation matrix; inspection reports live defaults, components, event graphs, locals, overrides, and graph types. Editor lifecycle, builds, Blueprint reparenting, project-settings mutation, filesystem access, console access, unrestricted reflection, and code execution remain unavailable.

## Security model

The plugin binds its HTTP route to `127.0.0.1` only and authenticates every request with a per-project, 64-hex-character high-entropy token. The token is generated under `<Project>/Saved/UnrealMCP/bridge.token`, atomically persisted, restricted to the owning user on Unix hosts, and re-read before the bridge becomes ready. Any token, listener, route, or heartbeat startup failure disables the bridge.

`Saved/UnrealMCP/discovery.json` contains only a project hash, process ID, port, bridge version, Unreal version, and heartbeat time. It never contains the token or absolute project path. The Python client rejects malformed, oversized, stale, or dead-process records and never connects to a non-loopback host.

Treat the project `Saved/` directory as generated state and keep it out of source control. Never copy `bridge.token` into an MCP configuration, log, issue, or repository.

## Install

1. Copy [`plugin/UnrealMCP`](plugin/UnrealMCP) to `<YourProject>/Plugins/UnrealMCP` or add this repository's `plugin/` folder as an `AdditionalPluginDirectories` entry in a disposable development `.uproject`.
2. Enable the `UnrealMCP` plugin and compile the project's Editor target with Unreal 5.8 or a newer version that passes the included public-API probes.
3. Open the project. Look for `Unreal MCP 0.14.0 ready on 127.0.0.1:15485` in the editor log.
4. Install the Python package offline from this folder:

   ```sh
   python3 -m venv .venv
   .venv/bin/python -m pip install --no-build-isolation --no-deps .
   ```

The default port is `15485`. To select another unprivileged port, add this to the project's editor-per-project configuration before startup:

```ini
[UnrealMCP]
Port=15486
```

Only one bridge may own a configured port. A bind or duplicate-route failure is fail-closed.

## LM Studio

Use an absolute `.uproject` path. The committed [`examples/lm-studio.json`](examples/lm-studio.json) shows the complete entry:

```json
{
  "mcpServers": {
    "unreal-editor": {
      "command": "/absolute/path/to/venv/bin/unreal-editor-mcp",
      "args": ["/absolute/path/to/Project.uproject"]
    }
  }
}
```

Start the Unreal project before calling a tool. `capabilities` remains available to diagnose an exact-version mismatch; other operations reject the mismatch. MCP stdout contains protocol messages only, while diagnostics go to stderr.

## Blueprint-family inspection

Read-only discovery covers every mounted content namespace visible to the project: project content under `/Game`, engine content under `/Engine`, and enabled plugin content under each plugin's virtual mount. Omitting `package_path` searches across all of them within the published scan ceiling. Prefer an exact, narrow mount/package filter when known:

```json
{
  "mode": "discover",
  "package_path": "/Game/Actors",
  "asset_name": "BP_Door",
  "page_size": 10
}
```

For example, an enabled plugin whose mount point is `/MyGameplayPlugin` can be searched with `"package_path": "/MyGameplayPlugin"`. Plugin mount names are not necessarily the same as their disk folder names.

Mutation tools intentionally use a narrower policy: they may change only `/Game` assets and content mounted from a plugin physically located in the current project's `Plugins/` directory. A local plugin mount must remain below a symlink-free plugin directory containing a `.uplugin` descriptor. `/Engine`, engine plugins, marketplace plugins installed outside the project, arbitrary external mounts, and symlink escapes remain read-only. `capabilities.asset_access` reports this split.

Inspect one exact asset after discovery. The shallow default returns summary, parent, compile state, components, variables, functions, macros, custom events, function-local variables, and graph summaries. Request parameter or graph details only when needed:

```json
{
  "mode": "inspect",
  "asset_path": "/Game/Actors/BP_Door.BP_Door",
  "sections": ["summary", "components", "variables", "graphs", "nodes", "pins", "connections"],
  "page_size": 50
}
```

Set `include_inherited` to include Blueprint-ancestor variables, callables, components, and native components. Stable `graph_id`, `component_id`, `member_id`, `function_id`, `local_id`, `macro_id`, and `custom_event_id` filters select one corresponding record and its related parameter records. Add one-to-32 `property_names` and the `class_defaults` section for targeted reflected-default read-back. The committed [`examples/inspection-queries.json`](examples/inspection-queries.json) contains focused examples.

Results are flat records with a `section` discriminator and a structural `snapshot_id`. A partial result supplies a single-use `next_cursor`; continue it within 30 seconds using only:

```json
{"cursor": "0123456789abcdef0123456789abcdef", "page_size": 50}
```

The cursor is bound to the original normalized query and snapshot. If graph structure, identities, defaults, or links change before continuation, the call returns `stale_precondition`. Re-inspect after compile, undo/redo, reload, or node reconstruction even when Unreal retained the same GUIDs.

Component, variable, function, local-variable, macro, custom-event, graph, node, and pin records use Unreal GUIDs where available and report `identity_stable: false` rather than inventing an ID otherwise. Callable records keep user functions, overrides, interfaces, macros, ordinary custom events, custom-event overrides, and inherited declarations distinct. They return complete signatures, metadata, graph relationships, required nodes, and bounded references. Supported properties use compact bounded Boolean, finite-number, string/name/text, enum/flags, common struct, and compatible visible asset/class reference encodings. Unsupported fields remain explicit with `supported: false`; arbitrary UObject graphs are never serialized.

Discovery asset records include `blueprint_family` and `native_family_class`. Every exact inspection page includes `blueprint_family` and `family_capabilities`; these fields remain present on cursor continuations. Generic Actor-derived Blueprints report `actor`. Descendants of `AGameModeBase`, `AGameMode`, `AGameStateBase`, and `AGameState` report `game_mode_base`, `game_mode`, `game_state_base`, and `game_state` respectively. `UGameInstance` descendants report `game_instance`; their summary record sets `actor_blueprint` to false.

## GameMode and GameState families

The four gameplay-framework families reuse the complete Actor-derived path: creation, targeted defaults, local SCS components, variables, function/local shells, macros, custom events, live action discovery, graph editing, compile diagnostics, saving, operation reconciliation, and restart read-back. Check `capabilities.blueprint_families` before authoring; `parent_change` and `project_settings_assignment` are explicitly false for every family.

Useful GameMode defaults include `GameStateClass`, `PlayerControllerClass`, `DefaultPawnClass`, `bUseSeamlessTravel`, and, for `AGameMode`, `bDelayedStart` and `MinRespawnDelay`. GameState families support safe inherited Actor defaults and `ServerWorldTimeSecondsUpdateFrequency`. Property support remains a live reflected decision, so inspect a targeted `class_defaults` property before writing it.

Use the action catalog for inherited framework behavior instead of guessing an override node. Representative actions include GameMode login/match callbacks and callable functions such as `GetDefaultPawnClassForController` or `GetMatchState`; GameState families expose inherited Actor events and state/time functions such as `GetServerWorldTimeSeconds`, `HasBegunPlay`, `HasMatchStarted`, and `HasMatchEnded`. Unreal's live graph filter decides what is valid in the selected graph.

Local components use the same ownership rules as Actor Blueprints: local SCS components are editable; inherited and native components are read-only. The bridge saves the Blueprint class but intentionally does not assign it as the project's active GameMode or GameState. Make that assignment manually in Unreal Project Settings or world settings after saving. See [`examples/game-mode-game-state-workflow.json`](examples/game-mode-game-state-workflow.json) for focused requests.

## GameInstance family

GameInstance uses the shared creation, inspection, default/member editing, action-catalog, graph-editing, compile, save, diagnostics, operation-reconciliation, and restart-read-back contracts. Its published inheritance category is `uobject_derived`. Live capabilities normally report class defaults, event graphs, local variables, overrides, and event/function/macro graph types as available, but `components` is always false. `blueprint_component_edit` returns `invalid_component` before opening a transaction or changing the snapshot.

A practical GameInstance default is a user-defined instance-editable variable such as a session region, profile slot, or matchmaking preference. Add the member, compile so the generated-class property exists, then use `blueprint_default_edit` with the newest snapshot. Inspect the exact property through `class_defaults` to verify it.

Use `blueprint_action_catalog` to discover callbacks exposed by `UGameInstance`, including `ReceiveInit` (`Init`), `ReceiveShutdown` (`Shutdown`), `HandleNetworkError`, and `HandleTravelError`. Add the returned action through `blueprint_graph_edit`; the live catalog suppresses a unique callback once it exists. The bridge saves the class but does not select it in Project Settings. Assign the saved GameInstance class manually until the narrow Phase 16 project-assignment operation is released. See [`examples/game-instance-workflow.json`](examples/game-instance-workflow.json).

## Blueprint action catalog

Use inspect first to obtain the current `snapshot_id`, one stable local `graph_id`, and, when needed, exact node/pin IDs. Then ask for the smallest useful action set. For example, discover the getter for one member in an event graph:

```json
{
  "asset_path": "/Game/Actors/BP_Door.BP_Door",
  "graph_id": "0123456789abcdef0123456789abcdef",
  "expected_snapshot": "0123456789abcdef0123456789abcdef01234567",
  "member": "Health",
  "node_family": "variable_get",
  "limit": 5
}
```

Optional filters are exact case-insensitive `text`, exact `owner_class`, `function`, `member`, one `node_family`, and an exact `pin_context` containing `node_id` and `pin_id`. The released families are:

- `function_call`, `variable_get`, and `variable_set` for callable and property actions, including local and inherited members;
- `event` for available inherited event overrides in event graphs, excluding unique events already implemented by the Blueprint;
- `flow_control` for branches, sequences, multi-gates, do-once nodes, switches, enum iteration, and context-valid standard flow-control macros;
- `cast` for object and class dynamic casts, with `owner_class` selecting the target class;
- `literal` for `MakeLiteral*`, enum, bitmask, and self-reference actions; and
- `operator` for common promotable or associative operators.

`function` applies to function-backed `function_call`, `event`, `literal`, and `operator` actions; `member` applies only to variable get/set actions. Every result has already passed Unreal's live Blueprint, graph, uniqueness, and optional pin filters. Function-backed records report pure, static, const, and latent flags; every record reports whether it is a wildcard candidate, and casts report whether they cast class references. Latent calls are excluded from incompatible function graphs, events are excluded from function and macro graphs, and pin context removes incompatible candidates. The catalog never accepts a node class or forged field/spawner signature.

Each result has an opaque `action_id`. Repeating an identical live query reuses IDs, but they expire after 60 seconds and are invalidated by retention eviction, structural snapshot changes, or editor/bridge restart. `blueprint_graph_edit` accepts the ID only for `add_node`; the plugin re-resolves its rebuildable signature and re-applies Unreal's live graph and optional pin filters immediately before invocation. Re-catalog whenever the ID lifetime or snapshot is uncertain.

Broad scans are intentionally bounded and report `truncated` and `timed_out`. Prefer an exact function/member and family, then add owner class or pin context when ambiguity remains. See [`examples/action-catalog-workflow.json`](examples/action-catalog-workflow.json) for inspect-first queries.

## Graph-node lifecycle

Inspect the target graph, catalog one exact action, then create a node using the same graph and snapshot identities:

```json
{
  "operation_id": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
  "asset_path": "/Game/Actors/BP_Door.BP_Door",
  "expected_snapshot": "0123456789abcdef0123456789abcdef01234567",
  "operation": "add_node",
  "graph_id": "0123456789abcdef0123456789abcdef",
  "action_id": "fedcba9876543210fedcba9876543210",
  "position": {"x": 160, "y": 240}
}
```

The result reports whether a node was newly `created` or a unique spawner `returned_existing`, plus the complete changed-node record, stable node/pin IDs, new snapshot, and dirty state. Creation is limited to 2,048 nodes per graph, results encode at most 256 pins per changed node, and each coordinate must be an integer from -1,000,000 through 1,000,000.

Use the returned node ID and latest snapshot to move or remove it:

```json
{
  "operation_id": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
  "asset_path": "/Game/Actors/BP_Door.BP_Door",
  "expected_snapshot": "89abcdef0123456789abcdef0123456789abcdef",
  "operation": "move_node",
  "graph_id": "0123456789abcdef0123456789abcdef",
  "node_id": "11111111111111111111111111111111",
  "position": {"x": 480, "y": -160}
}
```

`remove_node` uses the same shape without `position`. Only local event graphs, editable user-function graphs, and local macro graphs are mutable. Inherited, interface, construction, delegate/signature, intermediate, non-K2, and read-only graphs reject. Required entry/result/tunnel nodes, intermediate nodes, nodes without stable identity, and nodes Unreal does not consider user-deletable cannot be moved or removed. Removal breaks that node's links; reconnect surviving nodes explicitly with `connect_pins` when needed.

Every accepted edit is one Unreal transaction and remains dirty until `blueprint_save`. Re-inspect after each edit, compile, Undo/Redo, save/reload, or bridge restart; never reuse a prior action ID or snapshot. See [`examples/graph-node-lifecycle-workflow.json`](examples/graph-node-lifecycle-workflow.json) for the complete three-operation flow.

## Complete atomic pin and connection editing

Inspect `pins` after creating nodes and target every pin by its stable node and pin identities. Set one unlinked supported input default with the same tagged K2 values used by members and parameters:

```json
{
  "operation_id": "cccccccccccccccccccccccccccccccc",
  "asset_path": "/Game/Actors/BP_Door.BP_Door",
  "expected_snapshot": "0123456789abcdef0123456789abcdef01234567",
  "operation": "set_pin_default",
  "graph_id": "11111111111111111111111111111111",
  "node_id": "22222222222222222222222222222222",
  "pin_id": "33333333333333333333333333333333",
  "default": {"kind": "literal", "value": 77}
}
```

`engine_default` restores the pin's autogenerated default. Explicit Boolean, numeric, name/string/text/enum, and compatible hard/soft object/class/asset references are parsed by the live K2 schema. Defaults are limited to 512 canonical characters. The pin must be an editable, unlinked supported input; execution, wildcard, linked, orphaned, hidden/read-only/ignored, unstable, and stale pins reject without a transaction. Pin inspection reports the tagged `default` plus bounded raw string/object/text storage for precise read-back.

Create one direct output-to-input connection by supplying both node/pin identity pairs:

```json
{
  "operation_id": "dddddddddddddddddddddddddddddddd",
  "asset_path": "/Game/Actors/BP_Door.BP_Door",
  "expected_snapshot": "89abcdef0123456789abcdef0123456789abcdef",
  "operation": "connect_pins",
  "graph_id": "11111111111111111111111111111111",
  "from_node_id": "44444444444444444444444444444444",
  "from_pin_id": "55555555555555555555555555555555",
  "to_node_id": "22222222222222222222222222222222",
  "to_pin_id": "66666666666666666666666666666666"
}
```

The live schema decides exact compatibility and whether an exclusive input or execution output replaces existing links. Wildcard and numeric-promotion connections specialize through the schema without an extra flag; the result reports `wildcard_specialized` and every reconstructed node/pin identity. Directed cycles reject before a transaction.

Conversion-node insertion is disabled by default. Add `"automatic_conversion": true` only when one specific connection may insert a schema-selected conversion:

```json
{
  "operation_id": "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee",
  "asset_path": "/Game/Actors/BP_Door.BP_Door",
  "expected_snapshot": "0123456789abcdef0123456789abcdef01234567",
  "operation": "connect_pins",
  "graph_id": "11111111111111111111111111111111",
  "from_node_id": "44444444444444444444444444444444",
  "from_pin_id": "55555555555555555555555555555555",
  "to_node_id": "22222222222222222222222222222222",
  "to_pin_id": "66666666666666666666666666666666",
  "automatic_conversion": true
}
```

At most one conversion node may be inserted; its node and all pin IDs are returned in `created_identities` and `changed.nodes`. The connection record reports whether the result is `direct`, whether automatic conversion occurred, the conversion-node count, specialization, and replaced-link count. Each pin is limited to 64 links. `disconnect_pins` uses the same four identities, never accepts conversion opt-in, and requires the exact direct link. After every mutation or lost-response reconciliation, inspect the returned snapshot again before composing the next edit; also re-inspect after compile, Undo/Redo, reload, reconstruction, or bridge restart. See [`examples/complete-atomic-graph-workflow.json`](examples/complete-atomic-graph-workflow.json).

## Reliable Actor Blueprint mutation

Call `capabilities` before mutation and retain its `bridge_instance_id`. Generate a fresh 32-character lowercase hexadecimal `operation_id` for every intended mutation. Reusing the same ID and exact request returns its retained result without executing again; reusing it with different arguments returns `operation_conflict`.

If a mutation times out or its response is lost, do not retry it with a new ID. Reconcile first:

```json
{
  "operation_id": "0123456789abcdef0123456789abcdef",
  "bridge_instance_id": "fedcba9876543210fedcba9876543210"
}
```

`queued` may be cancelled by adding `"cancel": true`; `executing` is not interrupted unsafely. `committed` contains the retained result, while `rejected` contains the retained error. `outcome_unknown` means the bridge restarted or forgot the record: inspect the asset before deciding on another mutation.

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

Compilation and saving remain explicit. Both require `operation_id`, `asset_path`, and the latest `expected_snapshot`. `blueprint_compile` returns `compile_succeeded: false` rather than a tool error when the compiler completed and found Blueprint errors. `blueprint_save` does not compile implicitly. Re-inspect after compile because Unreal may reconstruct identities; save only the current returned snapshot.

See [`examples/creation-workflow.json`](examples/creation-workflow.json), [`examples/game-mode-game-state-workflow.json`](examples/game-mode-game-state-workflow.json), [`examples/game-instance-workflow.json`](examples/game-instance-workflow.json), [`examples/component-default-workflow.json`](examples/component-default-workflow.json), [`examples/member-variable-workflow.json`](examples/member-variable-workflow.json), [`examples/function-local-workflow.json`](examples/function-local-workflow.json), [`examples/macro-custom-event-workflow.json`](examples/macro-custom-event-workflow.json), [`examples/action-catalog-workflow.json`](examples/action-catalog-workflow.json), [`examples/graph-node-lifecycle-workflow.json`](examples/graph-node-lifecycle-workflow.json), and [`examples/pin-default-connection-workflow.json`](examples/pin-default-connection-workflow.json) for complete inspect-before-edit/discover sequences.

If saving fails, confirm that the package directory is writable and that source-control policy has not made the existing `.uasset` read-only. If compilation fails, inspect the returned diagnostics, correct the Blueprint in the editor or through later editing phases, compile again, and save only after `compile_succeeded` becomes true.

## Limits

The plugin publishes these authoritative defaults through `capabilities`: 64 KiB requests, 256 KiB responses, eight queued requests, JSON depth 16, strings up to 4096 characters, and a five-second Game-thread dispatch deadline. Inspection uses 25 records by default and allows 100 per page, scans at most 2,048 registry candidates, accepts at most 4,096 structural records, retains 32 cursors for 30 seconds, allows 32 targeted properties, returns at most 16 changed defaults per component, and lists at most 64 member or callable references. Action cataloging returns at most 50 results, scans at most 20,000 spawners for one second, retains 32 catalogs and 256 actions for 60 seconds, and permits one Game-thread catalog at a time. Graph editing permits 2,048 nodes per graph, 256 pins per changed-node result, integer coordinates within ±1,000,000, 64 links per pin, and 512 canonical pin-default characters. Function, macro, and custom-event signatures accept at most 32 parameters. K2 container defaults hold at most 64 items or map entries. The operation ledger retains 128 operations for 15 minutes. Compilation returns at most 64 diagnostic messages of 512 characters each. Discovery heartbeats are valid for ten seconds. Python HTTP calls default to three seconds and can be configured from `0.05` to `30` seconds.

## Offline development and tests

Configure `UNREAL_MCP_ENGINE_ROOT`, `UNREAL_MCP_TEST_UPROJECT`, and `UNREAL_MCP_DEVELOPER_DIR` as described in [`docs/development-environment.md`](docs/development-environment.md). The `ue-test/` directory is disposable and entirely ignored.

Run the dependency-free Python suite:

```sh
python3 -m unittest discover -s tests -v
```

Compile the plugin and all public Unreal API probes:

```sh
env DEVELOPER_DIR="$UNREAL_MCP_DEVELOPER_DIR" \
  "$UNREAL_MCP_ENGINE_ROOT/Engine/Build/BatchFiles/Mac/Build.sh" \
  UnrealMCPTestEditor Mac Development \
  -Project="$UNREAL_MCP_TEST_UPROJECT" -WaitMutex -NoHotReloadFromIDE
```

Run the Unreal Automation Tests:

```sh
python3 scripts/run_headless_integration.py --automation-only
```

Run the cross-process bridge acceptance test:

```sh
python3 scripts/run_headless_integration.py
```

The 0.14.0 native baseline is Unreal 5.8.0 on Apple Silicon macOS 26.5.2 with Xcode 26.1.1. Windows and Linux path/process branches are unit-tested through injected adapters; native Windows validation remains part of applicable feature work rather than a standalone roadmap gate.
