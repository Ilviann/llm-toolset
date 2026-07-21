# Unreal Editor MCP

Unreal Editor MCP 0.5.0 is an offline-first MCP bridge for Unreal Engine 5.8+. It pairs a dependency-free Python 3.10+ stdio server with an editor-only C++ plugin. This release exposes exactly ten tools:

- `capabilities` reports the exact Python/plugin/Unreal versions, commands, features, listener state, and effective limits.
- `editor_state` reports project identity, bridge readiness, play/simulate/save/GC state, and concise queued-operation state.
- `operation_status` reconciles or safely cancels one retained mutation by operation and bridge identity.
- `blueprint_inspect` discovers Actor Blueprints across mounted content and returns bounded pages of one selected Blueprint's structure.
- `blueprint_create` creates, compiles, saves, and verifies one new Actor Blueprint without overwriting content.
- `blueprint_compile` explicitly compiles one mutable Actor Blueprint and returns bounded diagnostics.
- `blueprint_save` explicitly saves one mutable Actor Blueprint package without interactive dialogs.
- `blueprint_component_edit` adds, removes, renames, reparents, roots, or edits one local Actor component.
- `blueprint_default_edit` edits one supported Blueprint-generated class-default property.
- `blueprint_member_edit` adds, renames, updates, or safely removes one typed Blueprint member variable.

Phase 5 supports reliable Actor Blueprint creation, component hierarchy/default editing, typed member-variable authoring, compilation, and saving. Function/macro/event and graph-node mutation, editor lifecycle, build, filesystem, console, unrestricted reflection, and code execution remain unavailable.

## Security model

The plugin binds its HTTP route to `127.0.0.1` only and authenticates every request with a per-project, 64-hex-character high-entropy token. The token is generated under `<Project>/Saved/UnrealMCP/bridge.token`, atomically persisted, restricted to the owning user on Unix hosts, and re-read before the bridge becomes ready. Any token, listener, route, or heartbeat startup failure disables the bridge.

`Saved/UnrealMCP/discovery.json` contains only a project hash, process ID, port, bridge version, Unreal version, and heartbeat time. It never contains the token or absolute project path. The Python client rejects malformed, oversized, stale, or dead-process records and never connects to a non-loopback host.

Treat the project `Saved/` directory as generated state and keep it out of source control. Never copy `bridge.token` into an MCP configuration, log, issue, or repository.

## Install

1. Copy [`plugin/UnrealMCP`](plugin/UnrealMCP) to `<YourProject>/Plugins/UnrealMCP` or add this repository's `plugin/` folder as an `AdditionalPluginDirectories` entry in a disposable development `.uproject`.
2. Enable the `UnrealMCP` plugin and compile the project's Editor target with Unreal 5.8 or a newer version that passes the included public-API probes.
3. Open the project. Look for `Unreal MCP 0.5.0 ready on 127.0.0.1:15485` in the editor log.
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

## Actor Blueprint inspection

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

Inspect one exact asset after discovery. The shallow default returns summary, parent, compile state, components, variables, and graph summaries. Request graph details only when needed:

```json
{
  "mode": "inspect",
  "asset_path": "/Game/Actors/BP_Door.BP_Door",
  "sections": ["summary", "components", "variables", "graphs", "nodes", "pins", "connections"],
  "page_size": 50
}
```

Set `include_inherited` to include Blueprint-ancestor variables/components and native components. Set `graph_id` to restrict graph records, `component_id` to select one stable component, or `member_id` to select one stable variable. Add one-to-32 `property_names` and the `class_defaults` section for targeted reflected-default read-back. The committed [`examples/inspection-queries.json`](examples/inspection-queries.json) contains discovery, shallow, targeted graph/default/member, and continuation examples.

Results are flat records with a `section` discriminator and a structural `snapshot_id`. A partial result supplies a single-use `next_cursor`; continue it within 30 seconds using only:

```json
{"cursor": "0123456789abcdef0123456789abcdef", "page_size": 50}
```

The cursor is bound to the original normalized query and snapshot. If graph structure, identities, defaults, or links change before continuation, the call returns `stale_precondition`. Re-inspect after compile, undo/redo, reload, or node reconstruction even when Unreal retained the same GUIDs.

Component, variable, graph, node, and pin records use Unreal GUIDs where available and report `identity_stable: false` rather than inventing an ID otherwise. Components and variables report ownership and editability. Variable records also return canonical K2 types/tagged defaults, metadata, replication/RepNotify relationships, and at most 64 loaded graph/node references. Supported properties use compact bounded Boolean, finite-number, string/name/text, enum/flags, common struct, and compatible visible asset/class reference encodings. Unsupported fields remain explicit with `supported: false`; arbitrary UObject graphs are never serialized.

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

`rename` preserves the member GUID. `update` changes exactly one of `type`, `default`, or `metadata`. A type update and every `remove` must include `"policy": "reject_if_referenced"`; referenced members return `referenced_member` without deleting nodes or changing the Blueprint. Inherited members are read-only. RepNotify function relationships are reported by inspection, but operations that would alter them are deferred until function authoring is available in Phase 6.

Compilation and saving remain explicit. Both require `operation_id`, `asset_path`, and the latest `expected_snapshot`. `blueprint_compile` returns `compile_succeeded: false` rather than a tool error when the compiler completed and found Blueprint errors. `blueprint_save` does not compile implicitly. Re-inspect after compile because Unreal may reconstruct identities; save only the current returned snapshot.

See [`examples/creation-workflow.json`](examples/creation-workflow.json), [`examples/component-default-workflow.json`](examples/component-default-workflow.json), and [`examples/member-variable-workflow.json`](examples/member-variable-workflow.json) for complete inspect-before-edit sequences.

If saving fails, confirm that the package directory is writable and that source-control policy has not made the existing `.uasset` read-only. If compilation fails, inspect the returned diagnostics, correct the Blueprint in the editor or through later editing phases, compile again, and save only after `compile_succeeded` becomes true.

## Limits

The plugin publishes these authoritative defaults through `capabilities`: 64 KiB requests, 256 KiB responses, eight queued requests, JSON depth 16, strings up to 4096 characters, and a five-second Game-thread dispatch deadline. Inspection uses 25 records by default and allows 100 per page, scans at most 2,048 registry candidates, accepts at most 4,096 structural records, retains 32 cursors for 30 seconds, allows 32 targeted properties, returns at most 16 changed defaults per component, and lists at most 64 variable references. K2 container defaults hold at most 64 items or map entries. The operation ledger retains 128 operations for 15 minutes. Compilation returns at most 64 diagnostic messages of 512 characters each. Discovery heartbeats are valid for ten seconds. Python HTTP calls default to three seconds and can be configured from `0.05` to `30` seconds.

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

The 0.5.0 native baseline is Unreal 5.8.0 on Apple Silicon macOS 26.5.2 with Xcode 26.1.1. Windows and Linux path/process branches are unit-tested through injected adapters; native Windows qualification remains a later roadmap gate.
