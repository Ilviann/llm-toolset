# Type: `MCPServer`

**Source:** `rooted_files_mcp/server.py`

Request/dispatch object constructed with `RootedFilesystem` and effective settings. It snapshots the permission-filtered catalog, handles initialization/ping/list/call methods, negotiates one of the supported protocol versions, suppresses notification responses, and maps tool names/arguments to the filesystem facade.

Expected missing arguments, file-access failures, and type failures become MCP `isError` text results. Unknown JSON-RPC methods/tool names and malformed requests/params use JSON-RPC errors. It never bypasses filesystem validation.
