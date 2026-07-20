# Rooted Files MCP Guidelines

Use `docs/index.md` as the implementation-knowledge entry point. Use `README.md` for user workflows, and executable source, package metadata, tool schemas, and behavioral tests as the authoritative behavior and compatibility contracts. Keep this file limited to durable workflow rules and invariants:

- Before inspecting or changing source, start at `docs/index.md`, read the owning file in `docs/architecture/`, then read the immediate `index.md` and relevant references under `docs/types/`.
- Keep one component per `docs/architecture/*.md` file. Keep custom data types, policy objects, records, and reusable function libraries in separate files under a component/module subfolder of `docs/types/`.
- Every directory under `docs/` must contain an `index.md`. Each index describes only its immediate files and subdirectories and links to them with relative paths.
- Update affected architecture/type/library references and their immediate indexes in the same change as source. Update `README.md` for user-visible operation or setup. Never recreate the removed `CODE.md` monolith.
- Follow `docs/workflow.md` for working-set selection, security-boundary review, verification, documentation, and release consistency.
- Keep model-facing paths root-relative and enforce permissions, root confinement, traversal/symlink denial, requested/resolved hidden policy, and `.mcp` protection in the filesystem service even when the MCP catalog filters access.
- Keep text operations bounded to validated UTF-8, reject binary names/signatures/NUL, and preserve line coordinates, BOMs, nearby newline conventions, final-newline state, and existing modes.
- Preserve same-directory atomic writes and repeat path, parent, hidden, existence, and existing-text validation immediately before replacement.
- Keep the tool set compact, schemas and dispatch synchronized, stdout protocol-only, diagnostics on stderr, and production dependency-free/offline.
- Test POSIX and Windows policy branches, including case sensitivity, hidden attributes, symlinks, atomic revalidation, and permission combinations.
