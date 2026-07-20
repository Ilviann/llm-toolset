# Library: newline-delimited stdio

**Source:** `run` in `rooted_files_mcp/server.py`

Reads one line per request, decodes JSON, requires a top-level object, invokes `MCPServer`, and writes one compact Unicode-preserving JSON response line with an immediate flush. Parse errors and invalid top-level values receive JSON-RPC errors. Notifications produce no output.

An unexpected per-request exception is reported to stderr and returned as a bounded internal error so the subprocess remains alive. Stdout must contain no startup or diagnostic prose.
