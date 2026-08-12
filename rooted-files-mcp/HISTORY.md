# Rooted Files MCP history

This file records released changes. Planned work is tracked separately in
[`ROADMAP.md`](ROADMAP.md).

## 0.5.2 — 2026-08-12

- Fixed stdio responses terminating the server on Windows when valid file
  content contained characters unavailable in the inherited console encoding.
- Made MCP stdin, stdout, and stderr explicitly strict UTF-8 and added a
  subprocess regression test under a forced ASCII inherited encoding.

## 0.5.1 — 2026-08-01

- Added a Windows CMD launcher for the MCP definition helper.
- Simplified the helper around the supported Python installation's available
  tkinter package.

## 0.5.0 — 2026-07-31

- Added an offline tkinter helper that selects the served root and
  `standard`/`markdown` mode, validates the launch paths, and displays a
  complete LM Studio-compatible `mcpServers` JSON object.
- Added individually copyable server name, Python command, and ordered argument
  fields for configuring a local STDIO server in the ChatGPT Codex app and
  other compatible agent harnesses.
- Added a POSIX shell launcher for starting the tkinter helper with `python3`.
- Added generator validation/formatting tests, user guidance, and process-entry
  architecture references.

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
