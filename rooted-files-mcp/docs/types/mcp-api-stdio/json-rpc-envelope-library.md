# Library: JSON-RPC and MCP text envelopes

**Source:** static helpers on `MCPServer` in `rooted_files_mcp/server.py`

- `_result(id, value)` creates a JSON-RPC 2.0 success envelope.
- `_error(id, code, message)` creates a JSON-RPC error envelope.
- `_tool_result(text, is_error=False)` creates one MCP text content item and marks expected failures with `isError`.

Tool outputs are already bounded/validated by filesystem policy. Keep envelopes compact and stable; diagnostics never belong in them unless they are the explicit safe public error text.
