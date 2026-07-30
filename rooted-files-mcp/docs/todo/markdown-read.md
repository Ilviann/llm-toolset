# `markdown-read` — Markdown section reads and read-only Markdown host mode

**Outcome:** Agents can retrieve one exact Markdown heading section or YAML
front-matter block without loading the complete document into model context, and
operators can expose a read-only host containing only the `read_text` tool and
Markdown files.

### Anchored section contract

- Accept a Markdown heading anchor in the root-relative `read_text.path`, for
  example `docs/setup.md#installation`.
- Split the fragment from the model-facing path without weakening existing path
  semantics. Resolve and validate the path portion with every existing
  permission, confinement, visibility, symlink, text-classification, UTF-8, and
  5 MiB check before interpreting Markdown.
- Match one documented GitHub-style generated heading anchor, including
  duplicate-heading suffixes such as `#example-1`. Define Unicode, punctuation,
  whitespace, case, escaping, and duplicate behavior precisely and keep it
  independent of the host filesystem's case policy.
- Return the matched heading and its complete section, including all nested
  subsections. Stop immediately before the next heading of the same or a higher
  level, or at end of file.
- Recognize ATX and Setext headings. Do not recognize heading-like text inside
  fenced code blocks, indented code blocks, or front matter.
- Preserve the selected source text exactly, including newline style and
  final-newline state where the selection reaches end of file.
- Return stable tool errors for empty, malformed, ambiguous, or missing anchors.
  Reject an anchored path combined with `start_line` or `end_line`.
- Preserve valid filenames containing `#` by interpreting a fragment only when
  the path portion before `#` has a supported Markdown extension.
- Keep existing whole-file and line-range behavior unchanged when no anchor is
  present.

### Front-matter contract

- Reserve the fragment `#---` for YAML front matter, for example
  `docs/file.md#---`. Paths remain root-relative, so `/docs/file.md#---`
  remains an invalid absolute path.
- Recognize front matter only when an exact `---` delimiter is the first logical
  line after an optional UTF-8 BOM.
- Return the complete front-matter block, including its opening and closing
  delimiter lines. Accept an exact `---` or `...` logical line as the closing
  delimiter.
- Preserve the source text exactly, including newline style and final-newline
  state. Do not interpret later horizontal rules as front matter or recognize
  headings inside the block.
- Return a stable front-matter-not-found tool error when either required
  delimiter is absent. Reject `#---` combined with `start_line` or `end_line`.
- Treat `---` as a reserved special fragment rather than a generated heading
  anchor.

### Markdown file and host-mode policy

- Support `.md` and `.markdown` files. Match these extensions without regard to
  letter case while retaining the host filesystem's existing path and
  case-sensitivity rules. Reject anchored reads for every other file type.
- Add `standard` and `markdown` host modes. Keep `standard` as the default with
  the current behavior.
- Expose the effective mode through `[features] mode` and
  `--mode {standard,markdown}`, following existing
  CLI-over-INI-over-built-in-default precedence.
- In `markdown` mode, publish only `read_text` when read permission is enabled
  and allow it to read only supported Markdown files, with or without a
  fragment.
- Keep directory listing, tree traversal, and all write operations unavailable
  in `markdown` mode. Reject direct calls to every disabled known tool.
- Publish an empty catalog when read permission is disabled rather than
  bypassing the permission setting.
- Enforce the Markdown-only restriction in the filesystem policy below MCP
  dispatch so direct internal calls cannot bypass it.
- Retain every existing security, text-validation, and resource-limit contract
  in both modes.

### Verification

- Cover exact section extraction, end-of-file sections, nested subsections, and
  termination at sibling or parent headings.
- Cover ATX and Setext headings; duplicate, Unicode, punctuation, whitespace,
  and case-derived anchors; fenced and indented code; CRLF; final newlines; and
  UTF-8 BOMs.
- Cover missing, empty, malformed, ambiguous, and unsupported-file anchors;
  anchors combined with line ranges; literal `#` filenames; and case variants
  of supported extensions.
- Cover empty and populated front matter, both closing delimiters, missing
  delimiters, horizontal rules elsewhere, heading-like front-matter content,
  BOMs, CRLF, and delimiter-like lines containing additional characters.
- Verify that the complete source file is validated even when only one section
  or front-matter block is retained and returned.
- Cover traversal, symlink escape, hidden and protected paths, invalid UTF-8,
  binary content, oversized files, read permission, and isolated POSIX/Windows
  policy branches.
- Verify exact `tools/list` schemas and direct-call rejection in both modes.
  Verify configuration validation and precedence, CLI and subprocess startup,
  and LM Studio-compatible initialization and tool-call framing.
- Run focused configuration, filesystem, and server suites, followed by the
  complete application suite and applicable native macOS, Linux, and Windows
  verification.

### Documentation and completion gate

- Document the exact anchor-generation algorithm, supported Markdown syntax,
  YAML front-matter convention, root-relative fragment examples, stable errors,
  supported extensions, and interaction with line ranges.
- Document standard and Markdown host modes in CLI, INI, and LM Studio examples.
  Update the measured schema-context cost for each relevant catalog.
- Update affected architecture pages, type references, immediate indexes,
  README, examples, version sources, and history.
- Complete the feature only when an agent can retrieve exact representative
  heading sections and front matter without whole-document output, Markdown
  mode cannot expose or access non-Markdown/read-write functionality, all
  existing security invariants remain enforced, and the full affected suite
  passes.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
