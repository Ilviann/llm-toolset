# Markdown path resolution

## Purpose

Authorize one existing regular Markdown file beneath the configured root before
any fragment is decoded or document content is interpreted.

## Owned source

- `markdown_mcp/paths.py` — extension recognition, final-fragment splitting,
  native absolute/relative resolution, confinement, file-type checks, and write
  revalidation.

## Data flow

The resolver first separates only a final fragment whose preceding path ends in
`.md` or `.markdown`. It validates the path portion, resolves it using native
platform semantics, checks containment with `Path.relative_to`, validates both
requested and resolved suffixes, and requires a regular file. The raw fragment
is returned separately for one-pass strict decoding by the Markdown component.

Write revalidation resolves the original plain model path again, compares its
canonical target and parent with the authorized target, and repeats every
extension, confinement, existence, and file-type check.

## Security invariants

- Relative and absolute paths have identical root authority.
- Sibling-prefix paths, traversal outside the root, symlink and junction
  escapes, NULs, missing paths, directories, special files, unsupported
  requested suffixes, and unsupported resolved suffixes fail with stable errors.
- Explicit hidden and dot-prefixed components are allowed.
- `#` remains filename data unless the portion before the final `#` already has
  a supported Markdown suffix.

## Verification

`tests/test_paths.py` covers fragment splitting, absolute and relative paths,
hidden names, literal hashes, traversal, sibling prefixes, file types,
extensions, symlink behavior when available, and native Windows junctions.
