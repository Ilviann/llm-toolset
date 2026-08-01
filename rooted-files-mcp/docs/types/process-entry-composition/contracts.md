# Process entry/composition contracts

Use the index to retrieve only the contract section relevant to the task.

## Library: process entry and composition

**Sources:** `server.py`, `rooted_files_mcp/__main__.py`, `rooted_files_mcp/server.py`

The root script and package module delegate to `main()`. `main()` defines the positional root, workspace, read/write, hidden-visibility, and `--mode {standard,markdown}` CLI; resolves immutable settings; and converts startup configuration/filesystem errors into argparse diagnostics. `run(settings)` accepts effective `Settings` or a legacy root, constructs `RootedFilesystem` and `MCPServer`, then owns the stdio loop.

Keep entry points behaviorally identical. New CLI policy belongs in configuration types first, then composition and README examples.

## MCP definition helper

**Source:** `scripts/generate_mcp_definition.py`

`build_server_definition` accepts a selected folder and exact `standard` or
`markdown` mode. It resolves that folder, the Python executable used to launch
the helper, and the repository's direct `server.py` launcher; all must exist
with the expected file or directory type. It returns a JSON-compatible STDIO
definition whose ordered arguments are the launcher, served root, `--mode`, and
the selected mode.

`format_mcp_json` wraps that definition as the `rooted-files` member of a
complete `mcpServers` object. The tkinter application displays this JSON plus
the server name, Python command, and each argument in separate copyable fields.
It does not write or merge an LM Studio `mcp.json`, Codex `config.toml`, or any
other host configuration.

`generate_mcp_definition.sh` resolves the adjacent Python helper from its own
location and replaces its process with `python3` running that helper.

`generate_mcp_definition.cmd` resolves the adjacent Python helper from its own
location, runs it with `python`, forwards any arguments, and returns the Python
process exit code.
