# Library: configuration file loading

**Source:** `rooted_files_mcp/configuration.py`

`_read_config_file` resolves only `.mcp/rooted-files-mcp.ini` below the workspace, requires a regular file, bounds metadata and read bytes to 64 KiB, rejects NUL, and decodes UTF-8 with optional BOM. `load_ini` parses with interpolation disabled and strict duplicate handling, then `_validate_schema` permits only `[paths] root`, `[permissions] read/write`, and `[features] show_hidden/hidden_allowlist`.

Unknown/default/duplicate/malformed input fails closed. Production code must never accept an alternate model-selected configuration path.
