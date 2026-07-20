# Library: rooted path resolution

**Source:** `RootedFilesystem.resolve` in `rooted_files_mcp/filesystem.py`

Validates a string, NUL-free, non-absolute model path; normalizes requested components; resolves it strictly or non-strictly according to operation policy; proves the result remains under root; and applies hidden policy to both requested and resolved paths. If resolution fails, name-only policy runs before the generic outside/missing error so protected/hidden direct access remains stable.

All public filesystem paths must pass through this function. Do not add alternate `Path` joining/resolution paths in text or listing code.
