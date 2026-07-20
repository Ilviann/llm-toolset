# Library: line replacement formatting

**Source:** `_replacement_bytes` and `write_lines` assembly in `rooted_files_mcp/filesystem.py`

Replacement content is normalized internally to LF, converted to the scan-selected newline convention, and given or denied a boundary terminator according to whether the range reaches EOF and whether the original ended with a newline. Empty content deletes the range.

Output reattaches a preserved UTF-8 BOM, byte-identical unchanged prefix/suffix lines, and the replacement. The complete result must fit 5 MiB. The library preserves file format; it does not normalize unrelated content.
