# Type: tool definition

**Source:** `TOOLS` in `rooted_files_mcp/server.py`

Compact dictionary with stable `name`, brief `description`, and dependency-free JSON `inputSchema`. The five records describe `list_dir`, `tree`, `read_text`, `write_text`, and `write_lines`; schemas use object properties, required fields, integer minima, defaults, and `additionalProperties: false`.

Names/arguments must match dispatch exactly. Keep descriptions short for small local-model context and enforce all security/type behavior again in the filesystem service.
