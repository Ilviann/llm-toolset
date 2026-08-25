# Markdown MCP project plan

## Status

This document records the agreed project description and requirements, plus a
proposed implementation. Requirements are confirmed unless explicitly labeled
as a proposal. Implementation has not started.

## Project description

`markdown-mcp` is a lightweight, offline-first, self-contained MCP server for
reading and editing existing Markdown files inside one configured root folder.
It is intended for LLM hosts, including LM Studio, and uses a small tool catalog,
bounded inputs and results, strict UTF-8, and no runtime dependency on Rooted
Files MCP or another application in this repository.

The server accepts both root-relative and absolute model-facing paths. Every
path must resolve to an existing regular Markdown file inside the configured
root. Absolute paths are a convenience and do not bypass root isolation.

## Confirmed requirements

### Startup and tool availability

The server has two startup parameters:

1. A required path to the root folder.
2. An optional `--writable` flag.

The default mode is read-only. It exposes only `read_markdown` and
`list_sections`. When `--writable` is present, the server also exposes
`overwrite_section`, `append_section`, `set_front_matter`, and
`delete_section`.

Tools unavailable in the effective mode must be omitted from `tools/list` and
rejected if called directly.

There is no INI configuration, workspace/root distinction, configurable
read/write permission set, hidden-file policy, or protected `.mcp` special
case. Explicitly addressed Markdown files beneath hidden or dot-prefixed
folders are accessible.

### File and path policy

- Support only `.md` and `.markdown`, matched case-insensitively.
- Operate only on existing regular files. File creation is out of scope.
- Accept absolute paths and paths relative to the configured root.
- Reject NUL-containing paths, traversal outside the root, and symbolic-link,
  junction, or other resolved-path escapes.
- Apply root confinement to the path portion before interpreting a Markdown
  fragment.
- Treat model input as untrusted and return stable, non-sensitive errors.
- Use a hardcoded `256 * 1024` byte limit for source files, resulting edited
  files, and semantic tool results.

### Encoding and format preservation

- Decode and validate complete files as strict UTF-8 before parsing or editing.
- Support and preserve an optional UTF-8 BOM.
- Configure MCP stdin, stdout, and stderr explicitly as strict UTF-8, including
  on Windows when the inherited locale encoding is not UTF-8.
- Preserve unaffected source text exactly, including newline style, final
  newline state, and existing file mode.
- Perform writes using same-directory atomic replacement and revalidate the
  path, parent, file type, extension, root confinement, and existing source
  immediately before replacement.
- Remove temporary files after failed writes and never expose a partial file.

### Markdown parsing

Use the established Rooted Files MCP Markdown behavior as the compatibility
baseline, while implementing it independently:

- Recognize ATX headings at levels 1 through 6.
- Recognize Setext headings at levels 1 and 2.
- Do not recognize heading-like text inside leading front matter, fenced code,
  or indented code.
- Generate deterministic GitHub-style, Unicode-preserving anchors.
- Apply collision suffixes such as `-1` and `-2` to duplicate anchors.
- A selected section starts at its heading and ends immediately before the next
  heading of the same or a higher level, or at end of file.
- Section reads include the heading, body, and every nested subsection.
- Decode a fragment once using strict UTF-8 percent decoding.
- Reserve `---` and `===` fragments for front matter rather than headings.

Only the final fragment after a path ending in a supported Markdown extension
is interpreted as a selector. This preserves valid `#` characters elsewhere in
a filename.

### Front matter

Front matter is recognized only when:

- its exact opening delimiter is the first logical line after an optional BOM;
- the opener is exactly `---` or `===`; and
- it has an exact matching closing delimiter.

The closing delimiter must match the opener. Both `#---` and `#===` are aliases:
either selector reads a valid leading block using either supported delimiter.
The returned block includes both delimiter lines.

Malformed leading front matter must not be silently rewritten as ordinary
document content by a front-matter mutation.

## MCP tool contracts

### `read_markdown`

Input:

```json
{"path": "string"}
```

Behavior:

- With no fragment, return the whole validated Markdown file.
- With a heading anchor, return that exact section, including its heading and
  all nested subsections.
- With `#---` or `#===`, return the complete valid leading front-matter block.
- Fail for missing headings, missing requested front matter, malformed
  fragments, unsupported extensions, invalid UTF-8, invalid paths, or limits.

### `list_sections`

Input:

```json
{"path": "string", "max_level": 3}
```

Behavior:

- Reject paths containing any fragment and always describe the whole file.
- `max_level` is an optional integer from 1 through 6 and defaults to 3.
- Return headings in source order whose absolute level is at most `max_level`.
- Return a flat list because each entry's level makes hierarchy derivable.
- Set `has_front_matter` for a valid leading `---` or `===` block.

Result shape:

```json
{
  "has_front_matter": true,
  "sections": [
    {"level": 1, "title": "Title", "anchor": "title"}
  ]
}
```

### `overwrite_section`

Input:

```json
{"path": "docs/file.md#existing-section", "body": "string"}
```

Behavior:

- Require a heading anchor and reject front-matter selectors.
- Require the selected section to exist.
- Preserve the existing heading source exactly.
- Replace everything after the heading through the selected section boundary,
  including all former nested subsections.
- An empty `body` clears the body and descendants but keeps the heading intact;
  it does not delete the section.

### `append_section`

Input:

```json
{"path": "docs/file.md#optional-parent", "title": "Title", "body": "string"}
```

Behavior:

- Accept `title` and `body` separately and generate an ATX heading.
- With no fragment, append a level-1 section at end of document.
- With a heading anchor, append a subsection at the end of the selected parent,
  after its current body and descendants and before the next peer or ancestor.
- Generate the child at exactly one level below the parent.
- Reject front-matter selectors and reject a level-6 parent because Markdown
  has no level-7 heading.
- Validate the title as one non-empty heading title without line breaks.

This is append-only. Arbitrary insertion before or after another section is not
part of the tool.

### `set_front_matter`

Input:

```json
{"path": "docs/file.md", "body": "string"}
```

Behavior:

- Accept only an unanchored file path.
- Accept the content between delimiters rather than a raw delimited block.
- If valid front matter exists and `body` is non-empty, replace its body while
  preserving the existing delimiter.
- If front matter is absent and `body` is non-empty, add a leading block using
  `---`.
- If `body` is empty and front matter exists, delete the complete block,
  including both delimiters.
- If `body` is empty and front matter is absent, succeed as an idempotent no-op.
- Reject a body containing an exact line equal to the delimiter that would wrap
  it, because that would create an ambiguous or prematurely closed block.

### `delete_section`

Input:

```json
{"path": "docs/file.md#existing-section"}
```

Behavior:

- Require a heading anchor and reject front-matter selectors.
- Require the selected section to exist.
- Delete the complete section: heading, body, and all nested subsections.
- Preserve all surrounding document content and formatting.

## Explicitly deferred scope

The initial release does not provide:

- Markdown file creation or deletion;
- arbitrary insertion before or after an existing section;
- moving or reordering sections;
- reordering or restructuring section groups;
- multi-section mutation in one call;
- generic text, directory-listing, or non-Markdown filesystem operations.

Insertion and reordering of section groups may be considered as a future
feature. The initial API does not reserve speculative schemas for it.

## Proposed implementation

### Application layout

```text
markdown-mcp/
|-- server.py
|-- pyproject.toml
|-- README.md
|-- AGENTS.md
|-- markdown_mcp/
|   |-- __init__.py
|   |-- configuration.py
|   |-- paths.py
|   |-- markdown.py
|   |-- filesystem.py
|   `-- server.py
|-- tests/
|   |-- test_configuration.py
|   |-- test_paths.py
|   |-- test_markdown.py
|   |-- test_filesystem.py
|   |-- test_server.py
|   `-- test_subprocess.py
`-- docs/
    |-- index.md
    |-- workflow.md
    |-- architecture/
    |-- types/
    `-- features/
```

Use only the Python standard library. Keep the application independently
installable and runnable through both its repository script and installed
console entry point.

### Component ownership

`configuration.py`

- Parse the required root and optional `--writable` flag.
- Resolve and validate the root once at startup.
- Produce immutable effective settings.

`paths.py`

- Split supported Markdown fragments from model-facing paths.
- Normalize relative and absolute paths without weakening platform semantics.
- Enforce extension, existing-regular-file, and resolved root-confinement
  policies.
- Provide a single authoritative resolver used by every operation and write
  revalidation.

`markdown.py`

- Retain exact source lines and endings.
- Detect front matter, fenced/indented code, ATX headings, and Setext headings.
- Generate anchors and section spans.
- Provide pure selection, listing, replacement, append, deletion, and
  front-matter splice calculations without accessing the filesystem.

`filesystem.py`

- Read and fully validate bounded UTF-8 files.
- Apply pure Markdown transformations.
- Enforce post-edit and result size limits.
- Preserve BOM, source formatting, and file mode.
- Own same-directory temporary files, flush/fsync, pre-replacement
  revalidation, atomic replacement, and cleanup.

`server.py`

- Define compact schemas for the six tools.
- Filter the catalog according to `--writable`.
- Validate and dispatch MCP requests.
- Return expected failures as bounded MCP tool errors.
- Keep stdout protocol-clean and diagnostics on stderr.
- Implement LM Studio-compatible initialization, `tools/list`, and
  `tools/call` framing with compact newline-delimited JSON.

### Edit construction

Edits should be calculated as exact source spans and applied in memory only
after the complete file has passed validation. Generated delimiters and ATX
headings should use the existing document's newline convention, falling back to
LF when the source has none. The editor may add only the minimum boundary
newline needed to keep generated Markdown structurally valid; it must not
normalize unrelated content.

Before replacing a file, repeat resolution and validation and ensure that the
target still represents the same authorized in-root regular Markdown path. A
concurrent path or file-type change must fail safely.

## Verification plan

### Parsing and selection

- Whole-file reads and exact anchored section reads.
- Sibling/ancestor termination and nested-subsection inclusion.
- ATX and Setext headings, closing ATX markers, duplicate headings, Unicode,
  punctuation, whitespace, percent escapes, and case-derived anchors.
- Fenced code variants, indented code, CRLF/LF, final-newline variants, and BOM.
- Empty, missing, malformed, reserved, and unsupported fragments.

### Front matter

- `---` and `===` blocks with matching closers.
- Both selector aliases with both delimiter forms.
- Empty and populated blocks, BOM, CRLF, and delimiter-like content.
- Mismatched, missing, or non-leading delimiters.
- Add, update, delete, and absent-delete no-op behavior.
- Delimiter injection rejection and exact preservation of surrounding content.

### Section mutation

- Body replacement with heading preservation.
- Empty-body clearing without heading deletion.
- Root-level and nested append with generated levels.
- Level-6 append rejection.
- Full parent deletion with descendants.
- Empty/end-of-file sections, adjacent headings, and formatting boundaries.
- Invalid title, missing section, front-matter selector, and unanchored calls.

### Filesystem and limits

- Relative and in-root absolute path equivalence on POSIX and Windows.
- Root itself, traversal, sibling-prefix confusion, symlink, junction, and
  case-sensitivity behavior.
- Hidden paths remain accessible.
- Unsupported extensions, directories, missing files, invalid UTF-8, NUL,
  binary-like content, and files/results on both sides of 256 KiB.
- Atomic revalidation races, temporary-file cleanup, mode preservation, and no
  partial writes.

### MCP and platform behavior

- Exact read-only and writable `tools/list` catalogs.
- Direct rejection of disabled mutation tools.
- Initialization, notifications, invalid JSON-RPC, missing arguments, expected
  tool errors, and unexpected internal errors.
- Protocol-clean stdout and bounded stderr diagnostics.
- Subprocess tests forcing a non-UTF-8 inherited Windows encoding while reading
  and returning otherwise unencodable Unicode.
- LM Studio-compatible initialization and tool calls on macOS, Linux, and
  Windows branches.

## Implementation sequence

1. Create the application skeleton, version metadata, startup settings, and
   read-only/writable catalog contract.
2. Implement authoritative path confinement, strict UTF-8 validation, and the
   256 KiB limits with security-focused tests.
3. Implement the pure Markdown parser, anchors, front matter, reads, and section
   listing with exact-source tests.
4. Implement in-memory section/front-matter transformations and atomic writes.
5. Complete MCP dispatch, strict UTF-8 stdio, subprocess framing, and Windows
   Unicode coverage.
6. Add user documentation, architecture/type references, feature metadata,
   examples, roadmap/history entries, and measured schema-context cost.
7. Run focused tests, the complete `markdown-mcp` suite, documentation lint,
   and applicable native platform verification before release.

## Completion criteria

The first release is complete when all six tool contracts behave as specified,
read-only mode cannot invoke mutations, every path and write remains confined
to the root, source and output are bounded to 256 KiB, Unicode is reliable under
non-UTF-8 Windows locales, edits preserve unaffected source formatting, MCP
framing works with LM Studio, affected documentation is synchronized, and the
full application test suite passes.
