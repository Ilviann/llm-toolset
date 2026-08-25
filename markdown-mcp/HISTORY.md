# Markdown MCP history

This file records released runtime changes. Planned work is tracked in
[`ROADMAP.md`](ROADMAP.md).

## 0.1.0 — 2026-08-25

- Added root-relative and in-root absolute reads for existing `.md` and
  `.markdown` files, including exact ATX/Setext sections and `---`/`===` front
  matter.
- Added bounded section listing with Unicode-preserving, collision-suffixed
  anchors.
- Added opt-in atomic section overwrite, append, deletion, and front-matter
  mutation while preserving BOMs, formatting, final-newline behavior, and file
  modes.
- Added read-only catalog enforcement, 256 KiB source/edit/result limits,
  strict UTF-8 stdio, root and resolved-path confinement, write revalidation,
  LM Studio-compatible framing, and dependency-free tests.
