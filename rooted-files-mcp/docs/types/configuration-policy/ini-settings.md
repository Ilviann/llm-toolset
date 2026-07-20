# Type: `IniSettings`

**Source:** `rooted_files_mcp/configuration.py`

Frozen intermediate dataclass containing only validated values explicitly present in `.mcp/rooted-files-mcp.ini`. Each field is optional so CLI/INI/default precedence can distinguish absence from `false`.

The optional root is already absolute, resolved, a directory, and confined to the workspace. The hidden allowlist remains an ordered tuple until native-case duplicate checks merge it with built-ins.
