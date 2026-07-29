# Unreal Editor MCP

Unreal Editor MCP 0.23.1 is an offline-first MCP bridge for Unreal Engine 5.8+. It pairs a dependency-free Python 3.10+ stdio server with an editor-only C++ plugin.

## Installation

### Windows graphical installer

Close Unreal Editor, then double-click:

```bat
scripts\deploy_plugin_windows.cmd
```

Select the folder containing the game's `.uproject`. Confirm or select the matching Unreal Engine 5.8+ installation, optionally enable **Include matching PDB crash symbols** for symbolicated plugin crash stacks, then choose **Build and install plugin**. The helper packages and installs the verified binary plugin at `<YourProject>\Plugins\UnrealMCP` without replacing an existing installation unless you approve it. PDB deployment is disabled by default and retains only a `Binaries/Win64` PDB whose basename matches a deployed DLL.

Python 3.10 or newer with tkinter is required. The build and installation are offline.

### Manual/source installation

1. Copy [`plugin/UnrealMCP`](plugin/UnrealMCP) to `<YourProject>/Plugins/UnrealMCP`, or add this repository's `plugin/` directory to `AdditionalPluginDirectories` in a disposable development `.uproject`.
2. Enable `UnrealMCP` and compile the project's Editor target with Unreal Engine 5.8 or newer.
3. Open the project and wait for `Unreal MCP 0.23.1 ready on 127.0.0.1:15485` in the editor log.
4. Create a virtual environment and install the Python package offline:

   ```sh
   python3 -m venv .venv
   .venv/bin/python -m pip install --no-build-isolation --no-deps .
   ```

To build a deployable binary plugin with the configured local Unreal toolchain, set `UNREAL_MCP_ENGINE_ROOT` and run:

```sh
python3 scripts/package_plugin.py
```

Windows Command Prompt users can run `scripts\package_plugin.cmd`. See the [detailed tool and deployment guides](docs/user/index.md) for packaging options, macOS requirements, custom ports, and optional editor lifecycle configuration.

### Optional large mode

Large mode is enabled with `--tool-mode large`. It adds the `editor_lifecycle` tool for the configured project. To allow launch and restart, also pass the absolute Unreal Editor executable with `--editor`; without it, large mode can report state and gracefully shut down an already-running editor, but launch and restart are unavailable.

For example, add the following arguments to an LM Studio MCP entry on macOS:

```json
"args": [
  "/absolute/path/to/Project.uproject",
  "--tool-mode",
  "large",
  "--editor",
  "/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
]
```

On Windows:

```json
"args": [
  "C:\\absolute\\path\\to\\Project.uproject",
  "--tool-mode",
  "large",
  "--editor",
  "C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor.exe"
]
```

Each item must remain a separate `args` value; paths containing spaces do not need shell quoting. Add `--lifecycle-timeout` followed by a value from 5 to 900 seconds to override the 120-second default. Native launch and restart are supported on macOS and Windows; Linux rejects those operations. See [Optional editor lifecycle](docs/user/tool-guides.md#optional-editor-lifecycle) for operation examples and safety behavior.

## Quickstart

1. Install and enable the plugin, then open the target Unreal project.
2. Add an LM Studio MCP entry using absolute paths. The committed [`examples/lm-studio.json`](examples/lm-studio.json) contains a complete example:

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
4. Call `capabilities` first. Confirm the Python/plugin/Unreal versions, listener readiness, effective limits, supported Blueprint families, and available commands.
5. Use inspection before mutation. Preserve returned asset, graph, member, pin, project, and snapshot identities; supply the latest required preconditions and a fresh 32-hex `operation_id` for each new mutation.
6. Call `blueprint_compile` and `blueprint_save` explicitly when authoring is complete. After a lost mutation response, reconcile the same operation through `operation_status` instead of retrying under a new ID.

Start Unreal before calling normal tools. `capabilities` remains available for diagnosing exact-version mismatches. MCP protocol messages use stdout; diagnostics use stderr.

## Contract overview

Default mode exposes twenty-one tools:

- Core and lifecycle state: `capabilities`, `editor_state`, and `operation_status`.
- Levels and assets: `level_inspect`, `level_open`, `asset_references`, and `asset_delete`.
- Blueprint discovery and authoring: `blueprint_inspect`, `blueprint_action_catalog`, `blueprint_graph_edit`, `blueprint_block_replace`, `blueprint_create`, `blueprint_compile`, `blueprint_save`, `blueprint_component_edit`, `blueprint_default_edit`, and `blueprint_member_edit`.
- Widget Blueprint hierarchy, layout, styling, binding, and Designer-event authoring: `widget_tree_edit`.
- Project and game data: `gameplay_framework_edit`, `game_data_inspect`, and `game_data_edit`.

Opt-in large mode adds `editor_lifecycle` for a single configured trusted project. It supports bounded launch, graceful shutdown, restart, and cancellation; it never accepts model-supplied executables, projects, process IDs, environment values, shell fragments, or arbitrary editor arguments.

The executable schemas, runtime `capabilities` response, source, plugin metadata, and behavioral tests are authoritative. Important cross-tool rules are:

- The plugin listens only on `127.0.0.1` and authenticates every request with a durable per-project token stored under `Saved/UnrealMCP`. Discovery never exposes the token or absolute project path.
- Model input is bounded and validated. Filesystem paths, force flags, arbitrary reflection, console commands, unrestricted serialization, and code execution are not exposed.
- Read operations return bounded pages tied to exact queries and snapshots. Continuation cursors are short-lived and single-use.
- Mutations use stale-state preconditions and a retained operation ledger. Reusing an operation ID with different arguments is rejected; unknown or partial outcomes require reconciliation before another mutation.
- Blueprint compilation and saving are explicit. A completed compile can return `compile_succeeded: false` with bounded diagnostics.
- Runtime-published limits override documentation. Current defaults include 64 KiB requests, 256 KiB responses, eight queued requests, JSON depth 16, a five-second Game-thread dispatch deadline, 100 inspection records per page, and 128 retained mutations for 15 minutes.

Detailed requests, workflows, safety conditions, and tool-specific limits are indexed under [`docs/user/`](docs/user/index.md). Implementation documentation starts at [`docs/index.md`](docs/index.md), and released changes are recorded in [`HISTORY.md`](HISTORY.md).
