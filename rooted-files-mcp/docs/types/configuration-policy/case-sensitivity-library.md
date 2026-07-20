# Library: filesystem case sensitivity

**Source:** `_filesystem_is_case_sensitive` in `rooted_files_mcp/configuration.py`

Detects native name matching without creating a probe file. Windows returns case-insensitive; other systems compare case-swapped existing path components when possible and fall back to platform behavior (macOS insensitive, other POSIX sensitive).

The result controls protected-name and allowlist duplicate comparisons plus case-insensitive actual-entry lookup. Tests must inject/cover Windows, macOS, and POSIX behavior independent of the host.
