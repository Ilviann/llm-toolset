# Unreal Editor MCP

Unreal Editor MCP 0.3.0 is an offline-first MCP bridge for Unreal Engine 5.8+. It pairs a dependency-free Python 3.10+ stdio server with an editor-only C++ plugin. This release exposes exactly six tools:

- `capabilities` reports the exact Python/plugin/Unreal versions, commands, features, listener state, and effective limits.
- `editor_state` reports project identity, bridge readiness, play/simulate/save/GC state, and concise queued-operation state.
- `blueprint_inspect` discovers Actor Blueprints across mounted content and returns bounded pages of one selected Blueprint's structure.
- `blueprint_create` creates, compiles, saves, and verifies one new Actor Blueprint without overwriting content.
- `blueprint_compile` explicitly compiles one mutable Actor Blueprint and returns bounded diagnostics.
- `blueprint_save` explicitly saves one mutable Actor Blueprint package without interactive dialogs.

Phase 3 mutation is limited to Actor Blueprint creation, compilation, and saving. There are no component/member/graph mutation, editor lifecycle, build, filesystem, console, general reflection, or code-execution commands in 0.3.0.

## Security model

The plugin binds its HTTP route to `127.0.0.1` only and authenticates every request with a per-project, 64-hex-character high-entropy token. The token is generated under `<Project>/Saved/UnrealMCP/bridge.token`, atomically persisted, restricted to the owning user on Unix hosts, and re-read before the bridge becomes ready. Any token, listener, route, or heartbeat startup failure disables the bridge.

`Saved/UnrealMCP/discovery.json` contains only a project hash, process ID, port, bridge version, Unreal version, and heartbeat time. It never contains the token or absolute project path. The Python client rejects malformed, oversized, stale, or dead-process records and never connects to a non-loopback host.

Treat the project `Saved/` directory as generated state and keep it out of source control. Never copy `bridge.token` into an MCP configuration, log, issue, or repository.

## Install

1. Copy [`plugin/UnrealMCP`](plugin/UnrealMCP) to `<YourProject>/Plugins/UnrealMCP` or add this repository's `plugin/` folder as an `AdditionalPluginDirectories` entry in a disposable development `.uproject`.
2. Enable the `UnrealMCP` plugin and compile the project's Editor target with Unreal 5.8 or a newer version that passes the included public-API probes.
3. Open the project. Look for `Unreal MCP 0.3.0 ready on 127.0.0.1:15485` in the editor log.
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

Set `include_inherited` to include content owned by Blueprint-generated ancestors. Set `graph_id` to restrict graph, node, pin, and connection records to one exact graph identity. The committed [`examples/inspection-queries.json`](examples/inspection-queries.json) contains discovery, shallow, targeted graph, and continuation argument examples.

Results are flat records with a `section` discriminator and a structural `snapshot_id`. A partial result supplies a single-use `next_cursor`; continue it within 30 seconds using only:

```json
{"cursor": "0123456789abcdef0123456789abcdef", "page_size": 50}
```

The cursor is bound to the original normalized query and snapshot. If graph structure, identities, defaults, or links change before continuation, the call returns `stale_precondition`. Re-inspect after compile, undo/redo, reload, or node reconstruction even when Unreal retained the same GUIDs.

Component, variable, graph, node, and pin records use Unreal GUIDs where available and report `identity_stable: false` rather than inventing an ID otherwise. Common K2 types and changed component defaults have compact bounded encodings. Unsupported categories and reflected properties remain explicit with `supported: false`; arbitrary UObject graphs are never serialized.

## Actor Blueprint creation, compile, and save

Create a Blueprint with one exact parent class and one destination long package name. Native parents use `/Script/Module.Class`; Blueprint parents use their generated class path ending in `_C`:

```json
{
  "parent_class": "/Script/Engine.Actor",
  "package_path": "/Game/Actors/BP_Door"
}
```

The parent must be Actor-derived and usable as a Blueprint base. Missing, non-Actor, abstract, deprecated, skeleton, reinstanced, editor-only, compiling, and compile-error parents reject before package creation. The destination must not include an object suffix. Any loaded object, package, registry asset, or file already occupying the destination returns `already_exists`; the tool never generates an alternate name or overwrites content.

Creation compiles and saves before publishing the asset. A mandatory compile failure returns `compile_failed`; a save operation failure returns `save_failed`; a read-only file or unwritable destination returns `write_conflict`. Before publication, any failure removes only the new asset/file and releases its requested package namespace so the same destination can be retried. Existing assets are never deleted during failure cleanup.

Compile and save an existing mutable Actor Blueprint explicitly:

```json
{"asset_path": "/Game/Actors/BP_Door.BP_Door"}
```

`blueprint_compile` returns `compile_succeeded: false` rather than a tool error when the compiler completed and found Blueprint errors, preserving its bounded structured diagnostics. `blueprint_save` does not compile implicitly. Every successful tool result contains the exact asset/parent paths, compile state, dirty state, saved/compiled flags, snapshot ID, and diagnostic counts. Use a separate `blueprint_inspect` call for detailed read-back. See [`examples/creation-workflow.json`](examples/creation-workflow.json) for the complete create → compile → save → inspect argument sequence.

If saving fails, confirm that the package directory is writable and that source-control policy has not made the existing `.uasset` read-only. If compilation fails, inspect the returned diagnostics, correct the Blueprint in the editor or through later editing phases, compile again, and save only after `compile_succeeded` becomes true.

## Limits

The plugin publishes these authoritative defaults through `capabilities`: 64 KiB requests, 256 KiB responses, eight queued requests, JSON depth 16, strings up to 4096 characters, and a five-second Game-thread dispatch deadline. Inspection uses 25 records by default and allows 100 per page, scans at most 2,048 registry candidates, accepts at most 4,096 structural records, retains 32 cursors for 30 seconds, and returns at most 16 changed defaults per component. Compilation returns at most 64 diagnostic messages of 512 characters each. Discovery heartbeats are valid for ten seconds. Python HTTP calls default to three seconds and can be configured from `0.05` to `30` seconds.

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

The 0.3.0 native baseline is Unreal 5.8.0 on Apple Silicon macOS 26.5.2 with Xcode 26.1.1. Windows and Linux path/process branches are unit-tested through injected adapters; native Windows qualification remains a later roadmap gate.
