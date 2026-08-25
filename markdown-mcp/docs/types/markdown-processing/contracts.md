# Markdown processing contracts

## `MarkdownDocument`

**Source:** `markdown_mcp/markdown.py`

The immutable parsed document retains the original logical text, exact source
lines, cumulative character offsets, source-ordered `Heading` records, an
optional valid `FrontMatter`, and a malformed-leading-front-matter flag. It has
no filesystem or byte authority.

## `Heading`

A heading records absolute level, heading start line, heading-source end line,
complete section end line, semantic title, and generated anchor. ATX heading
source is one line; Setext heading source includes its title and underline. The
complete section end is the next peer/ancestor start or end of file.

## `FrontMatter`

A valid block records its exact `---` or `===` delimiter and end line. Either
reserved selector aliases the block. An exact leading opener without an exact
matching closer is malformed, not absent.

## Fragment and anchor functions

`decode_fragment` validates percent syntax, decodes one UTF-8 layer, and rejects
empty selectors, whitespace, controls, `/`, `\`, and `#`. `base_anchor`
implements the documented Unicode-preserving visible-title normalization.
`parse_markdown` assigns collision suffixes while building section spans.

## Selection and listing functions

`select_markdown` returns exact source for a heading section or valid front
matter. `section_listing` returns `has_front_matter` and flat source-ordered
entries through an absolute heading-level filter from 1 through 6.

## Transformation functions

`overwrite_section`, `append_section`, `delete_section`, and `set_front_matter`
calculate one new logical string from exact spans. They reject missing or
reserved heading selectors, invalid titles, level-6 parents, malformed front
matter, and delimiter injection as applicable. `MarkdownError` is stable and
safe for conversion to a tool error.
