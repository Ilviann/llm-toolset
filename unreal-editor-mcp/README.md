# Unreal Editor MCP

Unreal Editor MCP 0.1.0 is an offline-first, read-only MCP bridge for Unreal Engine 5.8+. It pairs a dependency-free Python 3.10+ stdio server with an editor-only C++ plugin. This first release exposes exactly two tools:

- `capabilities` reports the exact Python/plugin/Unreal versions, commands, features, listener state, and effective limits.
- `editor_state` reports project identity, bridge readiness, play/simulate/save/GC state, and concise queued-operation state.

There are no Blueprint mutation, editor lifecycle, build, filesystem, console, reflection, or code-execution commands in 0.1.0.

## Security model

The plugin binds its HTTP route to `127.0.0.1` only and authenticates every request with a per-project, 64-hex-character high-entropy token. The token is generated under `<Project>/Saved/UnrealMCP/bridge.token`, atomically persisted, restricted to the owning user on Unix hosts, and re-read before the bridge becomes ready. Any token, listener, route, or heartbeat startup failure disables the bridge.

`Saved/UnrealMCP/discovery.json` contains only a project hash, process ID, port, bridge version, Unreal version, and heartbeat time. It never contains the token or absolute project path. The Python client rejects malformed, oversized, stale, or dead-process records and never connects to a non-loopback host.

Treat the project `Saved/` directory as generated state and keep it out of source control. Never copy `bridge.token` into an MCP configuration, log, issue, or repository.

## Install

1. Copy [`plugin/UnrealMCP`](plugin/UnrealMCP) to `<YourProject>/Plugins/UnrealMCP` or add this repository's `plugin/` folder as an `AdditionalPluginDirectories` entry in a disposable development `.uproject`.
2. Enable the `UnrealMCP` plugin and compile the project's Editor target with Unreal 5.8 or a newer version that passes the included public-API probes.
3. Open the project. Look for `Unreal MCP 0.1.0 ready on 127.0.0.1:15485` in the editor log.
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

## Limits

The plugin publishes these authoritative defaults through `capabilities`: 64 KiB requests, 256 KiB responses, eight queued requests, JSON depth 16, strings up to 4096 characters, and a five-second Game-thread dispatch deadline. Discovery heartbeats are valid for ten seconds. Python HTTP calls default to three seconds and can be configured from `0.05` to `30` seconds.

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

The 0.1.0 native baseline is Unreal 5.8.0 on Apple Silicon macOS 26.5.2 with Xcode 26.1.1. Windows and Linux path/process branches are unit-tested through injected adapters; native Windows qualification remains a later roadmap gate.
