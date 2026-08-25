# Markdown MCP

Markdown MCP is a small, dependency-free MCP server for reading and optionally
editing existing Markdown files beneath one configured root folder. It is
offline-first, uses newline-delimited JSON-RPC over stdio, and is suitable for
LM Studio and other MCP hosts.

Version: **0.1.0**. Python 3.10 or newer is required.

## Run from the repository

No installation or runtime download is needed:

```sh
python /path/to/llm-toolset/markdown-mcp/server.py /path/to/markdown/root
```

The default is read-only. Add `--writable` to publish editing tools:

```sh
python /path/to/llm-toolset/markdown-mcp/server.py /path/to/markdown/root --writable
```

The root is required and must already be a folder. There is no configuration
file, account, network access, telemetry, file creation, file deletion, or
directory-listing tool.

## Install the console command

The application has no runtime dependencies. Its build backend is pinned to
`setuptools==80.9.0`. For an offline installation, download that wheel on a
connected machine and copy the wheelhouse with the repository:

```sh
python -m pip download setuptools==80.9.0 -d wheelhouse
python -m pip install --no-index --find-links wheelhouse ./markdown-mcp
```

Then run `markdown-mcp ROOT [--writable]`.

## LM Studio configuration

Read-only example:

```json
{
  "mcpServers": {
    "markdown": {
      "command": "python",
      "args": [
        "/absolute/path/to/markdown-mcp/server.py",
        "/absolute/path/to/markdown/root"
      ]
    }
  }
}
```

Add `"--writable"` to the `args` array to enable edits. On Windows, JSON paths
may use forward slashes or escaped backslashes.

## Tools

Read-only mode exposes:

- `read_markdown({"path": "guide.md#installation"})` reads a whole file, one
  exact heading section, or leading front matter through `#---` or `#===`.
- `list_sections({"path": "guide.md", "max_level": 3})` returns
  `has_front_matter` and a flat source-ordered list of heading levels, titles,
  and anchors. `max_level` defaults to 3 and accepts 1 through 6.

Writable mode additionally exposes:

- `overwrite_section` preserves the selected heading and replaces its body and
  descendants.
- `append_section` appends a level-1 section to a file or exactly one level
  below a selected parent.
- `set_front_matter` adds, replaces, deletes, or idempotently leaves absent
  leading front matter.
- `delete_section` deletes a selected heading, body, and descendants.

Mutation tools operate only on existing files. A tool omitted in read-only mode
is also rejected if called directly.

## Paths, fragments, and Markdown syntax

Paths may be root-relative or absolute. Absolute paths are only a convenience:
the resolved existing regular file must remain beneath the configured root.
Hidden and dot-prefixed folders are accessible when explicitly addressed.
Only `.md` and `.markdown` suffixes are accepted, without regard to suffix case.
Traversal and symbolic-link or junction escapes are rejected.

Only the final `#fragment` after a supported Markdown suffix is a selector, so
filenames such as `draft#notes.md` remain valid. Fragments are percent-decoded
once as strict UTF-8. Whitespace, controls, `/`, `\`, `#`, malformed escapes,
and empty fragments are rejected.

The parser recognizes level 1–6 ATX headings and level 1–2 Setext headings.
Heading-like text in leading front matter, fenced code, or indented code is
ignored. A section includes its heading, body, and descendants and stops before
the next heading at the same or a higher level.

Anchors approximate GitHub heading anchors without a Markdown dependency:
visible link text replaces inline links, HTML tags and backticks are removed,
HTML entities and backslash escapes are decoded, whitespace collapses, text is
lowercased, spaces become `-`, Unicode is retained, punctuation except `-` and
`_` is removed, and duplicate anchors receive `-1`, `-2`, and later suffixes.

Leading front matter must start on the first logical line after an optional
UTF-8 BOM. The exact opener must be `---` or `===`, and the closer must exactly
match it. Both selector aliases read either valid form. Malformed leading front
matter is never reinterpreted by a front-matter edit.

## Preservation and limits

Every source is decoded completely as strict UTF-8 before use. A leading UTF-8
BOM is omitted from logical tool text and preserved by edits. Unaffected source
text, newline style, final-newline behavior, and file mode are preserved. Writes
use a flushed and fsynced same-directory temporary file, repeat path and source
validation, then atomically replace the original. Failed writes remove their
temporary files.

Source files, edited files, and semantic tool results have a hard 256 KiB UTF-8
limit. Paths are limited to 4,096 characters and generated heading titles to
1,000 characters through MCP. NUL-containing paths or documents and invalid
UTF-8 are rejected with bounded errors.

Compact UTF-8 catalog measurements for 0.1.0 are 923 bytes for read-only mode
and 2,092 bytes for writable mode.

## Test

From `markdown-mcp/`:

```sh
python -m unittest discover -s tests -v
```

From the repository root, validate all Markdown documentation with:

```sh
python internal/tools/lint_docs.py
```

The suite covers parser and mutation behavior, path confinement, source and
result limits, atomic revalidation and cleanup, catalog filtering, LM
Studio-compatible framing, and strict UTF-8 stdio under an inherited ASCII
encoding.
