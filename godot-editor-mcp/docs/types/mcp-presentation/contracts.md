# MCP presentation contracts

Use the index to retrieve only the contract section relevant to the task.

## Type: `ToolImageResult`

**Source:** `godot_editor_mcp/stdio.py`

Immutable Python value used to distinguish validated binary image output from ordinary JSON/text results.

| Field | Meaning |
| --- | --- |
| `data` | Complete PNG bytes already confined and validated by dispatch. |
| `mime_type` | MIME label; currently expected to be `image/png`. |
| `metadata` | Concise JSON object returned as adjacent text content. |

Only the capture dispatch path constructs this type. Stdio base64-encodes `data`; it does not repeat path, signature, dimension, or size validation. Keep the type internal to the trusted dispatcher/transport boundary.

## Types: request handler protocols

**Source:** `godot_editor_mcp/stdio.py`

`RequestHandler` defines `handle(request) -> response | None` for one decoded JSON-RPC object. `ClosableRequestHandler` extends it with `close()` so stdio shutdown can cancel waits and release collaborators.

The concrete `MCPServer` satisfies the closeable protocol structurally. Keep transport independent from the server class: tests and alternate in-process callers may inject lightweight handlers without inheritance.

## Library: JSON-RPC and MCP result envelopes

**Source:** `godot_editor_mcp/stdio.py`

Public helpers build compact protocol values:

- `result(id, value)` creates a JSON-RPC success envelope.
- `error(id, code, message)` creates a JSON-RPC error envelope.
- `tool_result(value, is_error=False)` creates MCP content: compact JSON text for ordinary values or PNG image plus metadata text for `ToolImageResult`.
- `serve(handler, input_stream, output_stream, error_stream)` owns line parsing, response writing, flushes, diagnostic routing, and final close.

These helpers must never emit diagnostics to stdout. JSON serialization must remain bounded by upstream tool/bridge limits.
