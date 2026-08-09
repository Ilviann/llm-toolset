# Limits and offline testing

## Limits

The plugin publishes these authoritative defaults through `capabilities`: 64 KiB requests, 256 KiB responses, eight queued requests, JSON depth 16, strings up to 4096 characters, and a five-second Game-thread dispatch deadline. Inspection uses 25 records by default and allows 100 per page, scans at most 2,048 registry candidates, accepts at most 4,096 structural records, retains 32 cursors for 30 seconds, allows 32 targeted properties, returns at most 16 changed defaults per component, and lists at most 64 member or callable references. Level actor editing accepts 32 operations resolving at most 64 actors, and explicit saving accepts 64 current-world packages; Actor tags are bounded to 64, Data Layers to 32, and components per Actor to 64. Asset-reference discovery examines at most 4,096 registry candidates and 8,192 loaded objects, retains at most 2,048 records and eight cursors, expands at most 64 assets per referencer package, reports at most 16 live property names, and traverses only direct references. Action cataloging returns at most 50 results, scans at most 20,000 spawners for one second, retains 32 catalogs and 256 actions for 60 seconds, and permits one Game-thread catalog at a time. Graph editing permits 2,048 nodes per graph, 256 pins per changed-node result, integer coordinates within ±1,000,000, 64 links per pin, and 512 canonical pin-default characters. Complete logic-unit replacement accepts at most 64 new nodes, 256 old owned nodes, 64 locals, 128 defaults, 256 internal connections, and 64 external connections. Automatic changed-node layout accepts at most 322 changed nodes and 1,024 derived edges, performs eight crossing-reduction iterations and at most 128 collision probes per node, consumes at most 2,000,000 work units and 100 milliseconds, and retains the shared coordinate bound. Function, macro, and custom-event signatures accept at most 32 parameters. K2 container defaults hold at most 64 items or map entries. Game-data schemas and nested structs contain at most 64 fields, containers hold at most 64 items, reflected values nest to depth four, one edit touches at most 64 rows, inspection refuses tables above 2,048 rows, and destructive schema scans examine at most 256 dependencies. The operation ledger retains 128 operations for 15 minutes. Compilation returns at most 64 diagnostic messages of 512 characters each. Discovery heartbeats are valid for ten seconds. Python HTTP calls default to three seconds and can be configured from `0.05` to `30` seconds.

## Offline development and tests

Configure `UE58` and point `UNREAL_MCP_TEST_UPROJECT` at `ue-test/ue58/UnrealMCPTest.uproject` as described in [`docs/development-environment.md`](../development-environment.md). macOS additionally requires `UNREAL_MCP_DEVELOPER_DIR`; Windows uses the configured engine's Win64 editor and installed Visual Studio toolchain. The parent `ue-test/` directory is disposable and entirely ignored.

Run the dependency-free Python suite:

```sh
python3 -m unittest discover -s tests -v
```

Compile the plugin and all public Unreal API probes:

macOS:

```sh
env DEVELOPER_DIR="$UNREAL_MCP_DEVELOPER_DIR" \
  "$UE58/Engine/Build/BatchFiles/Mac/Build.sh" \
  UnrealMCPTestEditor Mac Development \
  -Project="$UNREAL_MCP_TEST_UPROJECT" -WaitMutex -NoHotReloadFromIDE
```

Windows PowerShell:

```powershell
& "$env:UE58\Engine\Build\BatchFiles\Build.bat" `
  UnrealMCPTestEditor Win64 Development `
  "-Project=$env:UNREAL_MCP_TEST_UPROJECT" -WaitMutex -NoHotReloadFromIDE
```

Run the Unreal Automation Tests:

```sh
python3 scripts/run_headless_integration.py --automation-only
```

Run the cross-process bridge acceptance test:

```sh
python3 scripts/run_headless_integration.py
```

On Windows Command Prompt, use the matching runners; arguments such as `--automation-only` are forwarded to Python:

```bat
scripts\run_headless_integration.cmd --automation-only
scripts\run_headless_integration.cmd
```

The headless runner selects `UnrealEditor` on macOS and Linux and `UnrealEditor-Cmd.exe` on Windows. The prior 0.16.0 native baseline was Unreal 5.8.0 on Apple Silicon macOS 26.5.2 with Xcode 26.1.1. Platform selection and environment requirements are unit-tested without requiring every host.

Repository tooling tests target `scripts/packaging`, `scripts/windows_deployment`, and focused `scripts/headless_integration` owners. The historical `package_plugin.py`, `deploy_plugin_windows.py`, `run_headless_integration.py`, `game_data_levels.py`, and `companions.py` imports remain compatibility facades.
