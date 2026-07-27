# Rooted Files MCP Guidelines

Start at `docs/index.md` and follow `docs/workflow.md`. Use `README.md` for user guidance; executable contracts remain authoritative.

- Keep model-facing paths root-relative. Enforce permissions, root confinement, traversal/symlink denial, requested and resolved hidden-path policy, and `.mcp` protection in the filesystem service even when the catalog filters tools.
- Restrict text operations to validated UTF-8; reject binary names, signatures, and NUL. Preserve coordinates, BOM, newline style, final newline, and file mode.
- Use same-directory atomic replacement and immediately revalidate the path, parent, hidden state, existence, permissions, and existing text.
- Keep schemas and dispatch synchronized. Test POSIX and Windows policy branches, including case, hidden attributes, symlinks, atomic revalidation, and permission combinations.
