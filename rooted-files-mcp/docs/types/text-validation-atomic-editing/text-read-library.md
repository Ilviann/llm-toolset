# Library: text reads

**Source:** `RootedFilesystem.read_text` in `rooted_files_mcp/filesystem.py`

Requires read permission and a confined visible existing path. Without bounds, it returns the complete UTF-8 text with a leading BOM omitted. With bounds, both must be present and valid; the line scanner validates the entire file but retains/returns only exact selected logical lines.

There is no line-number decoration or newline normalization. Partial bounds, empty-file ranges, reversed/out-of-file coordinates, binary content, invalid UTF-8, and oversized sources fail safely.
