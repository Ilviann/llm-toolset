# Unreal Editor MCP

Unreal Editor MCP 0.42.0 is an offline-first MCP bridge for Unreal Engine 5.8+. It pairs a dependency-free Python 3.10+ stdio server with an editor-only C++ plugin.

Readonly access is the released default. Project-content mutation requires the explicit `--writable` trust decision; optional editor lifecycle control remains independent.

Optional independently versioned companion plugins can add bounded typed branches to existing tools through companion API v1. Native registrations are intersected with exact Python schemas; readonly mode exposes only inspection contributions and `--writable` remains mandatory for mutations. See the [companion plugin guide](docs/user/companion-plugins.md).

The optional `UnrealMCPGAS` 0.2.1 companion adds bounded read-only discovery and typed inspection of Gameplay Ability and data-only Gameplay Effect Blueprints without adding GAS dependencies to the base plugin. The Windows graphical deployment helper can build and install it alongside the base plugin. See the [Gameplay Ability](docs/user/gameplay-ability-blueprints.md) and [Gameplay Effect](docs/user/gameplay-effects.md) guides.

The optional `UnrealMCPCommonUI` 0.1.0 companion adds bounded read-only inspection of `UCommonUserWidget`-derived Widget Blueprints, including typed activation defaults and unresolved-safe CommonUI references. It keeps all CommonUI dependencies outside the base plugin and uses unchanged companion API v1. See the [CommonUI Widget inspection guide](docs/user/commonui-widget-blueprints.md).

## Installation

### Windows graphical installer

Close Unreal Editor, then double-click:

```bat
scripts\deploy_plugin_windows.cmd
```

Select the folder containing the game's `.uproject` and the matching Unreal Engine 5.8+ installation. Optionally select the independent **Unreal MCP GAS companion plugin** and **Unreal MCP CommonUI companion plugin** checkboxes, then choose project installation with explicit `.uproject` enablement, Engine installation enabled by default, or Engine installation without default enablement. Engine plugins are installed under `<Engine>\Engine\Plugins\Marketplace`; project plugins are installed under `<YourProject>\Plugins`. Existing selected-plugin installations require approval before replacement. Matching Win64 PDB deployment, writable MCP tools, and editor lifecycle control remain independent options. After installation, the tabbed configuration preview provides complete LM Studio JSON plus separately copyable ChatGPT Codex STDIO name, command, and argument fields.

Python 3.10 or newer with tkinter is required. The build and installation are offline.

### Manual/source installation

1. Copy [`plugin/UnrealMCP`](plugin/UnrealMCP) to `<YourProject>/Plugins/UnrealMCP`, or add this repository's `plugin/` directory to `AdditionalPluginDirectories` in a disposable development `.uproject`.
2. Enable `UnrealMCP` and compile the project's Editor target with Unreal Engine 5.8 or newer.
3. Open the project and wait for `Unreal MCP 0.42.0 ready on 127.0.0.1:15485` in the editor log.
4. Create a virtual environment and install the Python package offline:

   ```sh
   python3 -m venv .venv
   .venv/bin/python -m pip install --no-build-isolation --no-deps .
   ```

To build a deployable binary plugin with the configured Unreal Engine 5.8 toolchain, set `UE58` to the Engine installation root and run:

```sh
python3 scripts/package_plugin.py
```

Windows Command Prompt users can run `scripts\package_plugin.cmd`. See the [detailed tool and deployment guides](docs/user/index.md) for packaging options, macOS requirements, custom ports, and optional editor lifecycle configuration.

### Access and optional editor lifecycle

The server is readonly by default. Add `--writable` only when the MCP client is trusted to create, edit, compile, save, or delete content in this dedicated project. The flag is fixed for the server process and cannot be enabled by a tool call, environment variable, native capability, or editor setting.

Editor lifecycle control is independent. Add `--editor-lifecycle` followed by the absolute Unreal Editor executable to publish `editor_lifecycle` and configure launch/restart. This does not enable writable tools.

For example, add the following arguments to an LM Studio MCP entry on macOS:

```json
"args": [
  "/absolute/path/to/Project.uproject",
  "--editor-lifecycle",
  "/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
]
```

On Windows:

```json
"args": [
  "C:\\absolute\\path\\to\\Project.uproject",
  "--editor-lifecycle",
  "C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor.exe"
]
```

Each item must remain a separate `args` value; paths containing spaces do not need shell quoting. Add `--writable` to either example only when content mutation is required. Add `--lifecycle-timeout` followed by a value from 5 to 900 seconds to override the 120-second default. Native launch and restart are supported on macOS and Windows; Linux rejects those operations. See [Access configuration and migration](docs/user/setup-and-operation.md#access-configuration-and-migration) and [Optional editor lifecycle](docs/user/setup-and-operation.md#optional-editor-lifecycle).

## Quickstart

1. Install and enable the plugin, then open the target Unreal project.
2. Add an LM Studio MCP entry using absolute paths. The committed [`examples/lm-studio.json`](examples/lm-studio.json) contains the preferred readonly example; [`examples/lm-studio-writable.json`](examples/lm-studio-writable.json) is the explicit trusted-project alternative:

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

3. Start or reload the MCP server.
4. Call `capabilities` first. Confirm `access_mode`, the configured project identity, Python/plugin/Unreal versions, listener readiness, lifecycle availability, effective limits, supported Blueprint families, and available native commands.
5. Use inspection before mutation. To enable mutation, make the explicit trust decision to restart this MCP entry with `--writable`. Preserve returned asset, graph, member, pin, project, and snapshot identities; supply the latest required preconditions and a fresh 32-hex `operation_id` for each new mutation.
6. Call `blueprint_compile` and `blueprint_save` explicitly when authoring is complete. After a lost mutation response, reconcile the same operation through `operation_status` instead of retrying under a new ID.

Start Unreal before calling normal tools. When Unreal is unavailable, `capabilities` still returns the configured `.uproject` name and hash with `bridge_ready: false` and `native_capabilities_available: false`; native-only fields are absent. MCP protocol messages use stdout; diagnostics use stderr.

## Contract overview

Readonly mode is the default and exposes exactly nine tools:

- Core and lifecycle state: `capabilities`, `editor_state`, and `operation_status`.
- Levels and assets: `asset_references`, `level_inspect`, and `level_open`.
- Semantic asset and game-data inspection: `asset_inspect`, `blueprint_action_catalog`, and `game_data_inspect`.

`level_open` may change the active editor map, but it refuses dirty work and never saves, discards, overwrites, compiles, or dirties project content. Readonly operation may still maintain bounded generated discovery, cursor, lifecycle, and retained-operation records under `Saved/UnrealMCP/`; that generated state is not project-content write authority.

`--writable` exposes exactly twenty-five tools. In addition to the readonly set, it adds `operation_cancel`, `asset_delete`, `level_manage`, `level_actor_edit`, `level_save`, `blueprint_graph_edit`, `blueprint_block_replace`, `blueprint_create`, `blueprint_compile`, `blueprint_save`, `blueprint_component_edit`, `blueprint_default_edit`, `blueprint_member_edit`, `widget_tree_edit`, `gameplay_framework_edit`, and `game_data_edit`. `blueprint_block_replace` supports complete user functions, local macros, custom-event handlers, and native-event-rooted handlers with exact boundaries and either explicit positions or deterministic changed-node layout. `operation_status` only looks up a retained result; cancellation is the separate writable-only `operation_cancel` tool.

`--editor-lifecycle <absolute-executable>` independently appends `editor_lifecycle`, producing ten readonly-with-lifecycle tools or twenty-six writable-with-lifecycle tools. It supports bounded launch, graceful shutdown, restart, and cancellation; it never accepts model-supplied executables, projects, process IDs, environment values, shell fragments, or arbitrary editor arguments.

The executable schemas, runtime `capabilities` response, source, plugin metadata, and behavioral tests are authoritative. Important cross-tool rules are:

- The plugin listens only on `127.0.0.1` and authenticates every request with a durable per-project token stored under `Saved/UnrealMCP`. Discovery never exposes the token or absolute project path.
- Model input is bounded and validated. Filesystem paths, force flags, arbitrary reflection, console commands, unrestricted serialization, and code execution are not exposed.
- Read operations return bounded pages tied to exact queries and snapshots. Continuation cursors are short-lived and single-use.
- Writable tools use stale-state preconditions and a retained operation ledger. Reusing an operation ID with different arguments is rejected; unknown or partial outcomes require `operation_status` reconciliation before another mutation, while safe cancellation uses `operation_cancel`.
- Blueprint compilation and saving are explicit. A completed compile can return `compile_succeeded: false` with bounded diagnostics.
- Maps use mounted World object paths, never `.umap` filenames. `level_manage` creates or configures exact maps with explicit current-map snapshots and setup bounds; safe map deletion remains in `asset_delete` and verifies the complete owned package closure.
- `level_actor_edit` prevalidates one bounded stale-safe transaction and returns its exact dirty package set; `level_save` explicitly persists that set and verifies requested actor/component state by inspection or map reload.
- Runtime-published limits override documentation. Current defaults include 64 KiB requests, 256 KiB responses, eight queued requests, JSON depth 16, a five-second Game-thread dispatch deadline, 100 inspection records per page, and 128 retained mutations for 15 minutes.

Detailed requests, workflows, safety conditions, and tool-specific limits are indexed under [`docs/user/`](docs/user/index.md). Implementation documentation starts at [`docs/index.md`](docs/index.md), and released changes are recorded in [`HISTORY.md`](HISTORY.md).
