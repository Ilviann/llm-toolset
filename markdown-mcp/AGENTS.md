# Markdown MCP Guidelines

Start at `docs/index.md` and follow `docs/workflow.md`. Use `README.md` for
operator guidance; executable source, tool schemas, and behavioral tests define
the runtime contract.

- Keep every operation limited to existing regular `.md` and `.markdown` files
  whose resolved paths remain beneath the configured root.
- Preserve strict UTF-8, optional BOMs, unaffected source characters, file
  modes, newline style, and final-newline behavior.
- Keep source files, edited files, model inputs, and semantic outputs bounded;
  do not add directory or generic filesystem capabilities.
- Revalidate path authority and source bytes immediately before same-directory
  atomic replacement.
- Keep read-only and writable catalogs, dispatch, schemas, documentation, and
  subprocess framing tests synchronized.
