# Library: JSON-RPC and MCP result envelopes

**Source:** `godot_editor_mcp/stdio.py`

Public helpers build compact protocol values:

- `result(id, value)` creates a JSON-RPC success envelope.
- `error(id, code, message)` creates a JSON-RPC error envelope.
- `tool_result(value, is_error=False)` creates MCP content: compact JSON text for ordinary values or PNG image plus metadata text for `ToolImageResult`.
- `serve(handler, input_stream, output_stream, error_stream)` owns line parsing, response writing, flushes, diagnostic routing, and final close.

These helpers must never emit diagnostics to stdout. JSON serialization must remain bounded by upstream tool/bridge limits.
