# Setup and operation

For the complete tool inventory and links to the task guides, see [Tool guide overview](tool-guides.md).

## Security model

The plugin binds its HTTP route to `127.0.0.1` only and authenticates every request with a per-project, 64-hex-character high-entropy token. The token is generated under `<Project>/Saved/UnrealMCP/bridge.token`, atomically persisted, restricted to the owning user on Unix hosts, and re-read before the bridge becomes ready. Any token, listener, route, or heartbeat startup failure disables the bridge.

`Saved/UnrealMCP/discovery.json` contains only a project hash, process ID, port, bridge version, Unreal version, and heartbeat time. It never contains the token or absolute project path. The Python client rejects malformed, oversized, stale, or dead-process records and never connects to a non-loopback host.

Treat the project `Saved/` directory as generated state and keep it out of source control. Never copy `bridge.token` into an MCP configuration, log, issue, or repository.

## Install

### Windows graphical installer

Close Unreal Editor, then double-click:

```bat
scripts\deploy_plugin_windows.cmd
```

Select the folder that directly contains your game's `.uproject` file. When `UNREAL_MCP_ENGINE_ROOT` is set, its value is already shown in the Unreal Engine field and is preserved after project selection when valid. Otherwise, the helper detects a matching Unreal Engine 5.8+ installation from `EngineAssociation` and standard Epic/user-build registry records; use the second Browse button if manual selection is needed. For readable plugin frames in Windows crash reports, enable **Include matching PDB crash symbols (larger installation)**. Click **Build and install plugin**.

The helper uses the installed Engine and Visual Studio toolchain to package `Win64`, removes C++ implementation source and external debug/symbol artifacts by default, and installs the verified binary plugin at `<YourProject>\Plugins\UnrealMCP`. When PDB deployment is enabled, it retains only PDBs directly under `Binaries/Win64` whose basenames match deployed DLLs and rejects an installation missing a matching symbol file. The small `UnrealMCP.Build.cs` module rule and precompiled Unreal Build Tool metadata are retained, and the installed rule explicitly selects the packaged module so later game-project builds do not rebuild it. If that plugin folder already exists, the GUI asks before replacing it, and a failed build leaves the existing installation unchanged.

After installation, open the project and wait for the Unreal MCP ready message in the editor log. Use **Copy JSON** to copy the displayed `mcpServers` object and merge it into LM Studio's `mcp.json`. The generated entry uses the Python interpreter that launched the helper, this checkout's `server.py`, and the selected absolute `.uproject` path; it contains no bridge token. Keep this checkout and Python executable at those paths while LM Studio uses the entry.

Python 3.10 or newer with tkinter is required. Official Windows Python installers normally include tkinter. The build and installation are offline and may take several minutes.

### Manual/source installation

1. Copy [`plugin/UnrealMCP`](../../plugin/UnrealMCP) to `<YourProject>/Plugins/UnrealMCP` or add this repository's `plugin/` folder as an `AdditionalPluginDirectories` entry in a disposable development `.uproject`.
2. Enable the `UnrealMCP` plugin and compile the project's Editor target with Unreal 5.8 or a newer version that passes the included public-API probes.
3. Open the project. Look for `Unreal MCP 0.27.0 ready on 127.0.0.1:15485` in the editor log.
4. Install the Python package offline from this folder:

   ```sh
   python3 -m venv .venv
   .venv/bin/python -m pip install --no-build-isolation --no-deps .
   ```

### Package the binary plugin

Set `UNREAL_MCP_ENGINE_ROOT` to the Unreal installation root, then run the standard Unreal AutomationTool `BuildPlugin` workflow through the repository script:

```sh
python3 scripts/package_plugin.py
```

On Windows Command Prompt, the equivalent runner locates the Python script relative to itself and forwards any additional arguments:

```bat
scripts\package_plugin.cmd
```

On macOS, also set `UNREAL_MCP_DEVELOPER_DIR` to the compatible Xcode `Contents/Developer` directory. The script packages the plugin into `<workspace>/build/unreal-editor-mcp` by default, replacing that output directory's existing contents, and verifies that the result contains an installed plugin descriptor and compiled binaries. Copy the resulting directory to `<YourProject>/Plugins/UnrealMCP` for deployment against the same Unreal Engine version.

Use `--output <path>` to select another destination, `--target-platforms Mac` (or another `+`-separated UAT platform list) to restrict target platforms, and `--dry-run` to validate and print the fixed argument array without building. Run `python3 scripts/package_plugin.py --help` for all options. Packaging is offline and uses only the configured Unreal installation and local compiler toolchain.

The default port is `15485`. To select another unprivileged port, add this to the project's editor-per-project configuration before startup:

```ini
[UnrealMCP]
Port=15486
```

Only one bridge may own a configured port. A bind or duplicate-route failure is fail-closed.

## LM Studio

Use an absolute `.uproject` path. The committed [`examples/lm-studio.json`](../../examples/lm-studio.json) shows the complete entry:

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

Start the Unreal project before calling editor-backed tools. `capabilities` remains available while Unreal is inactive and returns the configured project name/hash, Python metadata, `bridge_ready: false`, and `native_capabilities_available: false`; native-only fields are absent. With an active bridge it also diagnoses exact-version mismatches. MCP stdout contains protocol messages only, while diagnostics go to stderr.

## Optional editor lifecycle

`editor_lifecycle` is absent from default mode. Enable it only for an MCP entry dedicated to one trusted project by adding `--tool-mode large` and an absolute `--editor` path. Windows requires `UnrealEditor.exe`; macOS requires the executable inside `UnrealEditor.app`. For example, the configured argument arrays end with:

```text
Windows: C:\absolute\Project.uproject --tool-mode large --editor C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe
macOS:   /absolute/Project.uproject --tool-mode large --editor /Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor
```

Pass each shown value as a separate MCP `args` element; spaces are part of a path, not shell quoting. `--lifecycle-timeout` configures one 5–900 second bound and defaults to 120 seconds. Linux rejects launch/restart; its command construction remains unit-tested without a native-support claim. Large mode without `--editor` can report and gracefully shut down the configured running project, but launch and restart reject as unavailable.

The tool accepts only:

```json
{"operation_id":"0123456789abcdef0123456789abcdef","operation":"launch"}
```

`operation` may be `launch`, `shutdown`, `restart`, or `cancel`. The model cannot supply an executable, project, process, port, environment value, shell fragment, or arbitrary editor argument. Launch is detached and reaches `ready` only after the launched process publishes this project's exact hash and version and passes authenticated capability verification. Repeated launch reports `already_running`.

Shutdown refuses while PIE/simulation, saving, garbage collection, an editor transaction, asset compilation, or any dirty package is present. Dirty refusal includes only a bounded package summary. It never saves, discards, prompts, force-kills, or retargets another process. Once shutdown is accepted, cancellation cannot interrupt it; restart can still cancel at the safe point after the old process exits and before the new launch.

Lifecycle progress is stored separately from Blueprint mutations at `Saved/UnrealMCP/lifecycle.json`, with at most 16 records retained for 24 hours. A server restart marks interrupted records `outcome_unknown`; inspect the configured editor state before choosing a new operation ID. Cancelling a launch wait does not terminate the editor, which may still become ready.
