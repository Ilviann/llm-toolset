# Library: runtime permissions

**Source:** `RootedFilesystem._require_read` and `_require_write` in `rooted_files_mcp/filesystem.py`

Every public operation invokes its owning permission helper before resolving or touching a model path. Read covers listing, tree, and whole/ranged reads; write covers full and ranged writes.

These checks intentionally duplicate MCP catalog filtering. Keep both layers synchronized so direct service calls or stale/crafted tool calls cannot bypass effective policy.
