# Markdown reading

## Purpose

Select one exact Markdown heading section or leading YAML front-matter block
after complete rooted text validation, and enforce the read-only Markdown host
policy below MCP dispatch.

## Owned source

- `rooted_files_mcp/markdown.py` — supported extensions, fragment decoding,
  heading/front-matter recognition, generated anchors, and exact extraction.
- Markdown integration and mode gates in
  `RootedFilesystem.read_text`, `list_dir`, `tree`, `write_text`, and
  `write_lines` in `rooted_files_mcp/filesystem.py`.

## Dependencies

The extraction library uses only the standard library and raises its own safe
selection error. The filesystem facade owns permission, mode, path,
visibility, symlink, text-classification, UTF-8, and size validation before it
calls extraction. Configuration owns the effective mode; MCP catalog filtering
duplicates the filesystem mode gate for defense in depth.

## Invariants

- Only the last fragment whose preceding path has a case-insensitive `.md` or
  `.markdown` suffix is interpreted; other `#` characters remain filename data.
- Fragment decoding is one-pass UTF-8 percent decoding. Generated anchors are
  exact, Unicode-preserving, filesystem-case-independent, and collision-suffixed.
- Sections begin at an ATX heading line or the title line of a Setext heading,
  include nested subsections, and end before the next peer/ancestor heading.
- Fenced code, indented code, and leading front matter cannot create headings.
- `#---` recognizes only an exact leading opener and exact `---`/`...` closer.
- Extraction joins retained source lines without newline normalization and only
  after the complete file passes the existing 5 MiB text-validation contract.
- Markdown mode permits only paths whose requested and resolved names have a
  supported Markdown suffix through `read_text`; directory and write operations
  fail below dispatch.

## Change and verification guide

Keep the README anchor algorithm, stable errors, syntax limits, tool
description, configuration mode, catalog filters, and filesystem gates
synchronized. Run focused configuration, filesystem, and server suites,
subprocess MCP framing, then the complete application suite.
