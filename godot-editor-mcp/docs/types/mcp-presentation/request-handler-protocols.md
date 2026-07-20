# Types: request handler protocols

**Source:** `godot_editor_mcp/stdio.py`

`RequestHandler` defines `handle(request) -> response | None` for one decoded JSON-RPC object. `ClosableRequestHandler` extends it with `close()` so stdio shutdown can cancel waits and release collaborators.

The concrete `MCPServer` satisfies the closeable protocol structurally. Keep transport independent from the server class: tests and alternate in-process callers may inject lightweight handlers without inheritance.
