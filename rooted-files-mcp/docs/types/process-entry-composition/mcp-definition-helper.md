# MCP definition helper

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
