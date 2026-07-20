# Library: line validation and scanning

**Source:** `_validate_line_range`, `_line_ending`, and `_scan_text_lines` in `rooted_files_mcp/filesystem.py`

Range validation accepts integers but rejects booleans, enforces one-based inclusive order, and later proves the end is within the total file line count. Scanning streams raw lines, enforces the whole-file byte/text policy, strips only the leading BOM from logical content, records terminators, optionally retains source bytes, and selects the requested text.

Nearby newline selection prefers the chosen range, then preceding lines, then following lines, then LF. Do not optimize ranged reads by skipping validation outside the requested range.
