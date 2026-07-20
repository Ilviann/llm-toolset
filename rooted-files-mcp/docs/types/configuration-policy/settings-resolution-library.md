# Library: settings resolution and precedence

**Source:** `rooted_files_mcp/configuration.py`

`load_settings` selects workspace from explicit workspace, positional root, or current directory; loads the fixed INI; resolves the effective root; detects case behavior; and merges each boolean as CLI value, then INI value, then default `true`.

An explicit positional root is trusted and may be outside an explicitly selected workspace. An INI root must remain within workspace. Configuration-only startup requires `[paths] root`; legacy positional-root startup may omit the file.
