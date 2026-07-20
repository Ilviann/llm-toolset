# Python MCP server

## Ownership

`unreal_editor_mcp/` owns the Python 3.10+ process. `stdio.py` bounds newline-delimited JSON-RPC and keeps stdout protocol-only. `server.py` negotiates MCP, publishes the two static tools, validates arguments, and converts domain failures to MCP tool errors. `project.py`, `platforms.py`, and `discovery.py` resolve one project and validate generated state. `bridge.py` is the only HTTP client. `cli.py` composes these responsibilities.

## Dependency direction

The CLI constructs a `ProjectLayout`, `UnrealBridge`, and `MCPServer`; the transport depends only on the server protocol. The server depends on an injected bridge protocol, not the concrete project or HTTP implementation. Discovery depends on an injected platform adapter so macOS, Windows, and Linux process/path behavior can be tested on one host. Everything uses the standard library.

## Invariants

- Only `capabilities` and `editor_state` appear in the tool catalog.
- Tool arguments are exact objects with no additional fields.
- HTTP always targets the literal IPv4 loopback address and authenticates with the generated token.
- Generated records and HTTP messages are read with explicit byte limits and strict record shapes.
- A stale heartbeat, dead process, unsafe token format, project identity change, timeout, or version mismatch produces a stable bounded error.
- `close()` closes active HTTP connections so stdio EOF cancels bounded work.

## Verification

Run `python3 -m unittest discover -s tests -v`. Changes to metadata, tool registration, discovery, HTTP, schema, errors, or stdio must update their focused tests and `test_contracts.py`.
