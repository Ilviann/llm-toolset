# Markdown processing

## Purpose

Parse validated logical text into exact source spans, generate deterministic
anchors, select semantic blocks, and calculate edits without filesystem access.

## Owned source

- `markdown_mcp/markdown.py` — strict fragment decoding, line retention, front
  matter, headings, anchors, section spans, listing, and all pure
  transformations.

## Parsing model

The parser separates only CR, LF, and CRLF logical lines while retaining their
source endings and cumulative offsets. It recognizes ATX levels 1–6 and Setext
levels 1–2 outside leading front matter, fenced code, and indented code. A
heading span includes its heading source; Setext heading source includes both
the title and underline. A section ends before the next peer or ancestor.

Valid front matter begins on logical line zero with exact `---` or `===` and
ends at the next exact matching delimiter. A leading opener without a matching
closer is recorded as malformed and suppresses heading interpretation inside
the unterminated block.

Anchors retain Unicode and approximate GitHub-visible heading text, then assign
collision suffixes in source order. Fragment decoding occurs exactly once as
strict UTF-8 and reserves `---` and `===` for front matter.

## Mutation invariants

- Section overwrite preserves heading source and replaces its body and every
  descendant.
- Append generates one ATX heading at level 1 or exactly one level below its
  parent; level 6 cannot have children.
- Delete removes the exact complete selected span.
- Front-matter edits preserve an existing delimiter and reject an exact body
  line that would close the generated block.
- Transformations preserve all source outside their calculated spans and add
  only required boundary newlines in the document's existing style.

## Verification

`tests/test_markdown.py` owns syntax, anchors, selection, front matter, newline,
final-newline, and pure mutation coverage.
