# Library: permission-filtered tool catalog

**Source:** `READ_TOOLS`, `WRITE_TOOLS`, `KNOWN_TOOLS`, and `build_tools` in `rooted_files_mcp/server.py`

Classifies the five stable tool names by required effective permission and returns catalog records in their original order only when enabled. `MCPServer` also retains the enabled-name set to reject stale or crafted calls to disabled tools.

Catalog groups, tool records, dispatch branches, README table, and tests must change together.
