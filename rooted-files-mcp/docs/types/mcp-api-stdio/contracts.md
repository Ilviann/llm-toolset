# MCP API/stdio contracts

Use the index to retrieve only the contract section relevant to the task.

## Type: tool definition

**Source:** `TOOLS` in `rooted_files_mcp/server.py`

Compact dictionary with stable `name`, brief `description`, and dependency-free JSON `inputSchema`. The five records describe `list_dir`, `tree`, `read_text`, `write_text`, and `write_lines`; schemas use object properties, required fields, integer minima, defaults, and `additionalProperties: false`.

Names/arguments must match dispatch exactly. Keep descriptions short for small local-model context and enforce all security/type behavior again in the filesystem service.

## Library: permission-filtered tool catalog

**Source:** `READ_TOOLS`, `WRITE_TOOLS`, `KNOWN_TOOLS`, and `build_tools` in `rooted_files_mcp/server.py`

Classifies the five stable tool names by required effective permission and
returns catalog records in their original order only when enabled. Standard
mode uses those permission groups; Markdown mode retains only `read_text` when
read permission is enabled. `MCPServer` also retains the enabled-name set to
reject stale or crafted calls to disabled tools.

Catalog groups, tool records, dispatch branches, README table, and tests must change together.

## Type: `MCPServer`

**Source:** `rooted_files_mcp/server.py`

Request/dispatch object constructed with `RootedFilesystem` and effective settings. It snapshots the permission-filtered catalog, handles initialization/ping/list/call methods, negotiates one of the supported protocol versions, suppresses notification responses, and maps tool names/arguments to the filesystem facade.

Expected missing arguments, file-access failures, and type failures become MCP `isError` text results. Unknown JSON-RPC methods/tool names and malformed requests/params use JSON-RPC errors. It never bypasses filesystem validation.

## Library: JSON-RPC and MCP text envelopes

**Source:** static helpers on `MCPServer` in `rooted_files_mcp/server.py`

- `_result(id, value)` creates a JSON-RPC 2.0 success envelope.
- `_error(id, code, message)` creates a JSON-RPC error envelope.
- `_tool_result(text, is_error=False)` creates one MCP text content item and marks expected failures with `isError`.

Tool outputs are already bounded/validated by filesystem policy. Keep envelopes compact and stable; diagnostics never belong in them unless they are the explicit safe public error text.

## Library: newline-delimited stdio

**Source:** `run` in `rooted_files_mcp/server.py`

`_configure_stdio` sets stdin, stdout, and stderr to strict UTF-8 independently
of the inherited host locale; output streams use literal newline framing. `run`
then reads one line per request, decodes JSON, requires a top-level object,
invokes `MCPServer`, and writes one compact Unicode-preserving JSON response
line with an immediate flush. Parse errors and invalid top-level values receive
JSON-RPC errors. Notifications produce no output.

An unexpected per-request exception is reported to stderr and returned as a bounded internal error so the subprocess remains alive. Stdout must contain no startup or diagnostic prose.
