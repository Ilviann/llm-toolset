# Rooted Files MCP history

This file records released changes. Planned work is tracked separately in
[`ROADMAP.md`](ROADMAP.md).

## 0.4.0 — 2026-07-31

- Added exact `read_text` selection of ATX/Setext Markdown heading sections and
  leading YAML front matter through root-relative fragments, with generated
  Unicode/duplicate anchors, URL escaping, source-format preservation, and
  stable selection errors.
- Added `standard` and read-only `markdown` host modes with CLI/INI precedence,
  a one-tool or empty catalog, supported-extension policy below MCP dispatch,
  and direct rejection of directory and write operations.
- Added configuration, filesystem-security, Markdown syntax/format, catalog,
  direct-call, subprocess, and LM Studio-compatible protocol coverage.

