# Library: Markdown selection

**Source:** `rooted_files_mcp/markdown.py`

`is_markdown_path` recognizes `.md` and `.markdown` without regard to suffix
case. `split_markdown_fragment` separates only the last raw fragment preceded by
a supported suffix. After the caller validates the complete file,
`decode_markdown_fragment` strictly decodes one layer of UTF-8 percent escaping
and returns stable `MarkdownReadError` failures for invalid selection syntax.

`extract_markdown` splits decoded text while retaining source endings, reserves
`---` for exact leading front matter, recognizes ATX and Setext headings outside
front matter/fenced/indented code, assigns deterministic GitHub-style
collision-free anchors, and returns an exact section. It does not access paths
or bytes; callers must complete filesystem and text validation first.
