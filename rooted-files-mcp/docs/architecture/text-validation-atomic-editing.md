# Text validation and atomic editing

## Purpose

Limit access to bounded UTF-8 text, support whole-file or validated one-based ranges, preserve file formatting, and atomically create/replace content without weakening confinement under path changes.

## Owned source

- Text classification, `_LineScan`, line scanning, `read_text`, write-target validation, replacement generation, atomic replacement, `write_text`, and `write_lines` portions of `rooted_files_mcp/filesystem.py`.

## Dependencies

Consumes effective settings plus the root/visibility component for permissions, resolution, hidden policy, parent confinement, and write-target revalidation. MCP dispatch exposes its four read/write paths through three model tools.

## Invariants

- Known binary/media extensions, common binary signatures, NUL bytes, invalid UTF-8, and files/results over 5 MiB are rejected consistently.
- Range bounds are integers but not booleans, one-based, inclusive, ordered, supplied together, and within the fully validated source file.
- Ranged reads return exact selected text without normalization or line-number decoration.
- Line writes preserve BOM, unchanged prefix/suffix bytes, selected/nearby newline convention, final-newline state, and existing file mode.
- Existing-file writes validate the old file even when read permission is disabled.
- Writes use a same-directory flushed/fsynced temporary file, then re-resolve/revalidate path, parent, hidden/text policy, and existence immediately before `os.replace`.
- Failure removes temporary files and never exposes partial output.

## Known pressure

Text scanning and atomic replacement are cohesive but make `filesystem.py` the largest context hotspot. A future split must allow text operations only on paths approved by the rooted policy and preserve pre-replacement revalidation.

## Change and verification guide

Review every whole/ranged read/write path together when classification or limits change. Run the full filesystem suite, especially binary/UTF-8/size, coordinates, mixed newline/BOM, mode preservation, failure cleanup, symlink/race revalidation, and write-only tests; then run server dispatch tests.
