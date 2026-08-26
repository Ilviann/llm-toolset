# Host configuration helper

## Purpose

Generate validated, copyable local launch configuration for LM Studio and
Codex without writing or merging either host's configuration files.

## Owned source

- `scripts/generate_mcp_config.py` — validation, configuration rendering, and
  Tkinter UI.
- `scripts/generate_mcp_config.sh` — macOS/Linux launcher.
- `scripts/generate_mcp_config.cmd` — Windows launcher.
- `tests/test_mcp_config_generator.py` — helper and generated-launch contracts.

## Dependencies

The helper reads the repository's direct `server.py` launcher and uses the
Python interpreter running the helper. It accepts one operator-selected
existing Markdown root and maps the selected access mode onto the startup
contract owned by [startup configuration](configuration.md).

## Invariants

- The root is an existing directory; the Python executable and server launcher
  are existing regular files. All three paths are resolved before rendering.
- Read-only definitions contain the launcher and root arguments. Writable
  definitions append exactly `--writable`.
- One validated definition supplies both the complete LM Studio `mcpServers`
  JSON object and the Codex `[mcp_servers.markdown]` TOML table.
- The UI copies text only on explicit operator action and never writes, merges,
  or replaces host configuration.
- The helper uses only the Python standard library and performs no network,
  account, installation, or telemetry operation.
- The launch wrappers resolve the adjacent Python helper from their own
  locations and preserve the Python process exit status.

## Change and verification guide

Review server argument ordering, read-only defaults, writable opt-in, JSON and
TOML escaping, platform launchers, and README instructions together. Run
`tests.test_mcp_config_generator`, the complete Markdown MCP suite, and the
repository documentation linter.
