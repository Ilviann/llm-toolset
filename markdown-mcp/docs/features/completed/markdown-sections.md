---
feature_id: markdown-sections
status: completed
depends_on: []
released_in: "0.1.0"
---

# `markdown-sections` — Rooted Markdown section tools

**Status:** Completed in 0.1.0. Native Windows verification passed; native
macOS and Linux execution remains in the roadmap platform backlog.

**Outcome:** MCP hosts can read whole existing Markdown files, exact heading
sections, front matter, and bounded section metadata beneath one configured
root. Operators may opt into source-preserving atomic section and front-matter
edits without exposing generic filesystem capabilities.

## Startup and catalog contract

- Require one existing root folder and default to read-only.
- Publish only `read_markdown` and `list_sections` unless `--writable` is
  present.
- In writable mode also publish `overwrite_section`, `append_section`,
  `set_front_matter`, and `delete_section`.
- Reject direct calls to mutation tools in read-only mode below catalog
  filtering.

## Path, text, and resource contract

- Accept root-relative and absolute paths only when the requested and resolved
  existing regular file remains beneath the root and has a case-insensitive
  `.md` or `.markdown` suffix.
- Allow explicitly addressed hidden paths while rejecting NULs, traversal,
  sibling-prefix confusion, symlink/junction escapes, unsupported suffixes,
  missing files, directories, special files, NUL content, and invalid UTF-8.
- Bound source files, edited files, body input, and semantic results to 256 KiB.
- Preserve an optional UTF-8 BOM, unaffected source characters, newline style,
  final-newline behavior, and file mode.
- Use same-directory atomic replacement after immediate path, parent, type,
  suffix, confinement, source-byte, identity, and mode revalidation; remove
  temporary files on every failure.

## Markdown read and listing contract

- Split only the final fragment after a supported Markdown suffix, then decode
  one strict UTF-8 percent-encoding layer.
- Recognize ATX levels 1–6 and Setext levels 1–2 outside leading front matter,
  fenced code, and indented code.
- Generate deterministic GitHub-style Unicode-preserving anchors with
  collision suffixes.
- Read the selected heading, body, and descendants through its peer/ancestor
  boundary.
- Recognize exact leading matching `---` or `===` blocks; alias either valid
  form through either reserved selector.
- List source-ordered headings through an optional absolute level 1–6 filter and
  report whether valid leading front matter exists.

## Mutation contract

- Overwrite a selected section body and descendants while preserving its
  heading source; an empty body clears but does not delete the heading.
- Append a generated level-1 ATX section at file end or an exact next-level
  child at parent end; reject invalid titles and level-6 parents.
- Add, update, delete, or idempotently leave absent front matter while
  preserving an existing delimiter and rejecting exact delimiter injection.
- Reject malformed leading front matter rather than rewriting it as document
  content.
- Delete one complete selected heading section and its descendants.
- Do not create/delete files, list directories, perform generic text edits,
  reorder groups, move sections, or mutate multiple sections per call.

## MCP and platform contract

- Use compact closed schemas and stable bounded tool errors.
- Return the section listing through matching structured and text content.
- Support MCP initialization, notifications, tools/list, tools/call, ping, and
  LM Studio-compatible newline-delimited JSON-RPC.
- Configure stdin, stdout, and stderr as strict UTF-8, including Windows hosts
  with non-UTF-8 inherited encodings.

## Verification

- Parser tests cover syntax variants, nested boundaries, anchors, duplicates,
  Unicode, escaping, code blocks, front matter, CRLF/LF, and final-newline cases.
- Mutation tests cover body clearing, descendants, root/child append, level 6,
  add/update/delete/no-op front matter, injection, and malformed blocks.
- Filesystem tests cover paths, UTF-8/BOM/NUL, limits, modes, absolute/hidden
  files, atomic failures, concurrent changes, and cleanup.
- MCP and subprocess tests cover exact catalogs, schemas, direct disabled calls,
  arguments, errors, initialization, notifications, framing, recovery, and
  Unicode output under inherited ASCII.
- The complete dependency-free suite and repository documentation linter must
  pass before release.

[Back to roadmap](../../../ROADMAP.md) · [Feature index](../index.md)
