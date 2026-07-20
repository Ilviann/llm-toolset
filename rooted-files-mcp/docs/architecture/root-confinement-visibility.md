# Root confinement and visibility policy

## Purpose

Enforce permissions and one authoritative filesystem boundary for model-facing paths; reject traversal/root/symlink escapes and hidden/protected paths; and expose bounded directory listings without following directory symlinks.

## Owned source

- `HiddenPathPolicy`, permission checks, `resolve`, `list_dir`, and `tree` portions of `rooted_files_mcp/filesystem.py`.

## Dependencies

Consumes immutable `Settings`, `ConfigurationError`, and `PROTECTED_NAMES`. Text validation/editing depends on its resolution and permission decisions. MCP dispatch exposes its public operations and duplicates permission filtering for defense in depth.

## Invariants

- Paths must be strings, relative to root, NUL-free, and confined after resolution.
- Hidden policy checks both normalized requested components and resolved in-root target components so visible symlink aliases cannot reveal hidden targets.
- `.mcp` is always hidden/protected, regardless of `show_hidden` or allowlist.
- Dot names are hidden on every platform when configured; Windows Hidden attributes are also enforced through an independently testable branch.
- Case comparisons follow detected native filesystem behavior.
- Listings prune denied entries without disclosing which component failed and do not count them against the 100-entry tree bound.
- Tree traversal never follows directory symlinks; later file reads still validate their targets.
- Read/write permissions are enforced inside the filesystem even if MCP catalog filtering is bypassed.

## Known pressure

`filesystem.py` combines this security domain with substantial text-editing logic. A future authorized refactor may split a low-level path/visibility collaborator from text access while retaining `RootedFilesystem` as the only public facade. It must not create an alternate unguarded path.

## Change and verification guide

Review configuration policy, both requested/resolved checks, symlink behavior, POSIX/Windows hidden branches, permissions, and stable errors. Run `tests.test_configuration`, the full `tests.test_filesystem`, and permission/catalog server tests.
