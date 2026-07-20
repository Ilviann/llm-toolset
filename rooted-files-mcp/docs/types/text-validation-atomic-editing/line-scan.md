# Type: `_LineScan`

**Source:** `rooted_files_mcp/filesystem.py`

Frozen internal record produced after validating a complete source file for a requested range.

| Field | Meaning |
| --- | --- |
| `selected_text` | Exact decoded text for the inclusive requested lines. |
| `line_count` | Total logical addressable lines in the file. |
| `has_bom` | Whether the source began with a UTF-8 BOM. |
| `ended_with_newline` | Whether the final raw line had a terminator. |
| `nearby_newline` | Selected/previous/following newline convention, falling back to LF. |
| `raw_lines` | Optional validated BOM-free raw source lines retained only for replacement. |

This record keeps read results concise while carrying enough format state for byte-preserving range edits.
