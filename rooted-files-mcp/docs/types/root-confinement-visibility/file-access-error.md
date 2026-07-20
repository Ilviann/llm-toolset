# Types: filesystem access errors

**Source:** `rooted_files_mcp/filesystem.py`

`FileAccessError` is the public expected-failure boundary returned as an MCP tool error. It covers permissions, invalid/outside/missing paths, hidden access, folder/file mismatch, text classification, range coordinates, size/encoding, and safe OS-operation messages.

Private `_HiddenPathError` marks a hidden/protected denial so listings can prune entries without disclosing which component was denied. Direct access exposes only the stable message `Hidden path access is denied`.
