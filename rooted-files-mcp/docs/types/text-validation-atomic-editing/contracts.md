# Text-validation/atomic-editing contracts

Use the index to retrieve only the contract section relevant to the task.

## Type: `_LineScan`

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

## Library: text classification

**Source:** `rooted_files_mcp/filesystem.py`

`_reject_binary_name` denies a fixed set of binary/media extensions even if bytes might decode. `_reject_binary_bytes` denies common executable/archive/image/audio/document signatures and any NUL. `_decode_utf8` accepts UTF-8 with optional BOM stripping. `_validate_text_path` and `_read_text_bytes` require a regular file and enforce the 5 MiB bound before and during the bounded read.

Whole reads, ranged reads, existing-target writes, and replacement revalidation share this classification. Change extension, signature, encoding, or size policy for every path together.

## Library: text reads

**Source:** `RootedFilesystem.read_text` in `rooted_files_mcp/filesystem.py`

Requires read permission and a confined visible existing path. Without bounds or
a fragment, it returns the complete UTF-8 text with a leading BOM omitted. With
bounds, both must be present and valid; the line scanner validates the entire
file but retains/returns only exact selected logical lines. With a supported
Markdown fragment, it fully validates and decodes the source before delegating
selection to the Markdown component.

There is no line-number decoration or newline normalization. Partial bounds, empty-file ranges, reversed/out-of-file coordinates, binary content, invalid UTF-8, and oversized sources fail safely.

## Library: line validation and scanning

**Source:** `_validate_line_range`, `_line_ending`, and `_scan_text_lines` in `rooted_files_mcp/filesystem.py`

Range validation accepts integers but rejects booleans, enforces one-based inclusive order, and later proves the end is within the total file line count. Scanning streams raw lines, enforces the whole-file byte/text policy, strips only the leading BOM from logical content, records terminators, optionally retains source bytes, and selects the requested text.

Nearby newline selection prefers the chosen range, then preceding lines, then following lines, then LF. Do not optimize ranged reads by skipping validation outside the requested range.

## Library: line replacement formatting

**Source:** `_replacement_bytes` and `write_lines` assembly in `rooted_files_mcp/filesystem.py`

Replacement content is normalized internally to LF, converted to the scan-selected newline convention, and given or denied a boundary terminator according to whether the range reaches EOF and whether the original ended with a newline. Empty content deletes the range.

Output reattaches a preserved UTF-8 BOM, byte-identical unchanged prefix/suffix lines, and the replacement. The complete result must fit 5 MiB. The library preserves file format; it does not normalize unrelated content.

## Library: atomic replacement

**Source:** `_write_target` and `_replace_atomically` in `rooted_files_mcp/filesystem.py`

Initial validation resolves the model path, rejects binary names, confines the real parent, and validates an existing target as text even in write-only mode. Publication writes a same-directory `.rooted-mcp-*` temporary file, flushes and `fsync`s it, copies the existing mode when applicable, then re-resolves the original model path and revalidates path, parent, existence, hidden policy, and existing text immediately before `os.replace`.

Path/parent changes or a disappeared required target fail. OS and policy failures remove the temporary file. This repeated validation is deliberate TOCTOU defense and must survive refactoring.
