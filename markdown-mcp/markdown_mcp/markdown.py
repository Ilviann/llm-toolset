"""Pure exact-source Markdown parsing, selection, and transformation."""

from __future__ import annotations

import html
import re
import unicodedata
from dataclasses import dataclass
from urllib.parse import unquote_to_bytes


_ATX_HEADING = re.compile(r"^ {0,3}(#{1,6})(?:[ \t]+(.*?)|[ \t]*)$")
_SETEXT_UNDERLINE = re.compile(r"^ {0,3}(=+|-+)[ \t]*$")
_FENCE_OPEN = re.compile(r"^ {0,3}(`{3,}|~{3,})(.*)$")
_LINK = re.compile(r"!?\[([^\]]*)\]\([^)]*\)")
_HTML_TAG = re.compile(r"<[^>\n]*>")
_ESCAPE = re.compile(r"\\([!\"#$%&'()*+,\-./:;<=>?@\[\\\]^_`{|}~])")
_PERCENT_ESCAPE = re.compile(r"%(?![0-9A-Fa-f]{2})")
_RESERVED_FRAGMENTS = frozenset({"---", "==="})


class MarkdownError(Exception):
    """A stable Markdown contract error safe to expose through MCP."""


@dataclass(frozen=True)
class Heading:
    level: int
    start: int
    heading_end: int
    end: int
    title: str
    anchor: str


@dataclass(frozen=True)
class FrontMatter:
    delimiter: str
    end: int


@dataclass(frozen=True)
class MarkdownDocument:
    text: str
    lines: tuple[str, ...]
    offsets: tuple[int, ...]
    headings: tuple[Heading, ...]
    front_matter: FrontMatter | None
    malformed_front_matter: bool

    def offset(self, line_index: int) -> int:
        return self.offsets[line_index]


def _split_lines(text: str) -> list[str]:
    """Split only CR, LF, and CRLF while retaining every source character."""

    lines: list[str] = []
    start = 0
    index = 0
    while index < len(text):
        char = text[index]
        if char not in "\r\n":
            index += 1
            continue
        end = index + 1
        if char == "\r" and end < len(text) and text[end] == "\n":
            end += 1
        lines.append(text[start:end])
        start = end
        index = end
    if start < len(text):
        lines.append(text[start:])
    return lines


def _logical_line(line: str) -> str:
    if line.endswith("\r\n"):
        return line[:-2]
    if line.endswith(("\r", "\n")):
        return line[:-1]
    return line


def newline_style(text: str) -> str:
    for line in _split_lines(text):
        if line.endswith("\r\n"):
            return "\r\n"
        if line.endswith("\r"):
            return "\r"
        if line.endswith("\n"):
            return "\n"
    return "\n"


def decode_fragment(fragment: str) -> str:
    """Strictly percent-decode one fragment layer as UTF-8."""

    if not isinstance(fragment, str) or not fragment:
        raise MarkdownError("Markdown fragment is empty")
    if _PERCENT_ESCAPE.search(fragment):
        raise MarkdownError("Markdown fragment is malformed")
    try:
        decoded = unquote_to_bytes(fragment).decode("utf-8")
    except (UnicodeDecodeError, UnicodeEncodeError):
        raise MarkdownError("Markdown fragment is malformed") from None
    if (
        not decoded
        or any(
            char.isspace() or unicodedata.category(char) in {"Cc", "Cf"}
            for char in decoded
        )
        or any(char in "/\\#" for char in decoded)
    ):
        raise MarkdownError("Markdown fragment is malformed")
    return decoded


def _visible_heading_text(title: str) -> str:
    title = _LINK.sub(lambda match: match.group(1), title)
    title = _HTML_TAG.sub("", title)
    title = _ESCAPE.sub(lambda match: match.group(1), title)
    title = title.replace("`", "")
    return " ".join(html.unescape(title).split())


def base_anchor(title: str) -> str:
    output: list[str] = []
    for char in _visible_heading_text(title).lower():
        category = unicodedata.category(char)
        if char == " ":
            output.append("-")
        elif char in "-_":
            output.append(char)
        elif category.startswith("P") or category in {"Cc", "Cf"}:
            continue
        else:
            output.append(char)
    return "".join(output)


def _is_indented_code(line: str) -> bool:
    columns = 0
    for char in line:
        if char == " ":
            columns += 1
        elif char == "\t":
            columns += 4 - (columns % 4)
        else:
            break
        if columns >= 4:
            return True
    return False


def _front_matter(lines: list[str]) -> tuple[FrontMatter | None, bool, int]:
    if not lines:
        return None, False, 0
    opener = _logical_line(lines[0])
    if opener not in _RESERVED_FRAGMENTS:
        return None, False, 0
    for index in range(1, len(lines)):
        if _logical_line(lines[index]) == opener:
            return FrontMatter(opener, index + 1), False, index + 1
    return None, True, len(lines)


@dataclass(frozen=True)
class _HeadingDraft:
    level: int
    start: int
    heading_end: int
    title: str
    anchor: str = ""


def _heading_drafts(lines: list[str], content_start: int) -> list[_HeadingDraft]:
    headings: list[_HeadingDraft] = []
    fence_character: str | None = None
    fence_length = 0
    previous_plain: tuple[int, str] | None = None

    for index in range(content_start, len(lines)):
        logical = _logical_line(lines[index])
        if fence_character is not None:
            if re.fullmatch(
                rf" {{0,3}}{re.escape(fence_character)}{{{fence_length},}}[ \t]*",
                logical,
            ):
                fence_character = None
                fence_length = 0
            previous_plain = None
            continue

        fence = _FENCE_OPEN.match(logical)
        if fence:
            marker = fence.group(1)
            if marker[0] == "~" or "`" not in fence.group(2):
                fence_character = marker[0]
                fence_length = len(marker)
                previous_plain = None
                continue

        if _is_indented_code(logical):
            previous_plain = None
            continue

        atx = _ATX_HEADING.match(logical)
        if atx:
            title = atx.group(2) or ""
            title = re.sub(r"[ \t]+#+[ \t]*$", "", title).strip(" \t")
            headings.append(
                _HeadingDraft(len(atx.group(1)), index, index + 1, title)
            )
            previous_plain = None
            continue

        setext = _SETEXT_UNDERLINE.match(logical)
        if setext and previous_plain is not None:
            previous_index, title = previous_plain
            headings.append(
                _HeadingDraft(
                    1 if setext.group(1)[0] == "=" else 2,
                    previous_index,
                    index + 1,
                    title,
                )
            )
            previous_plain = None
            continue

        if logical.strip():
            leading = len(logical) - len(logical.lstrip(" "))
            previous_plain = (index, logical[leading:]) if leading <= 3 else None
        else:
            previous_plain = None

    used: set[str] = set()
    next_suffix: dict[str, int] = {}
    anchored: list[_HeadingDraft] = []
    for heading in headings:
        base = base_anchor(heading.title)
        candidate = base
        suffix = next_suffix.get(base, 0)
        while candidate in used:
            suffix += 1
            candidate = f"{base}-{suffix}"
        next_suffix[base] = suffix
        used.add(candidate)
        anchored.append(
            _HeadingDraft(
                heading.level,
                heading.start,
                heading.heading_end,
                heading.title,
                candidate,
            )
        )
    return anchored


def parse_markdown(text: str) -> MarkdownDocument:
    lines = _split_lines(text)
    offsets = [0]
    for line in lines:
        offsets.append(offsets[-1] + len(line))
    front_matter, malformed, content_start = _front_matter(lines)
    drafts = _heading_drafts(lines, content_start)
    headings: list[Heading] = []
    for index, heading in enumerate(drafts):
        end = len(lines)
        for following in drafts[index + 1 :]:
            if following.level <= heading.level:
                end = following.start
                break
        headings.append(
            Heading(
                heading.level,
                heading.start,
                heading.heading_end,
                end,
                heading.title,
                heading.anchor,
            )
        )
    return MarkdownDocument(
        text,
        tuple(lines),
        tuple(offsets),
        tuple(headings),
        front_matter,
        malformed,
    )


def _find_heading(document: MarkdownDocument, fragment: str) -> Heading:
    if fragment in _RESERVED_FRAGMENTS:
        raise MarkdownError("A heading fragment is required")
    matches = [heading for heading in document.headings if heading.anchor == fragment]
    if not matches:
        raise MarkdownError("Markdown section was not found")
    if len(matches) > 1:
        raise MarkdownError("Markdown section is ambiguous")
    return matches[0]


def select_markdown(text: str, fragment: str) -> str:
    document = parse_markdown(text)
    if fragment in _RESERVED_FRAGMENTS:
        if document.front_matter is not None:
            return "".join(document.lines[: document.front_matter.end])
        if document.malformed_front_matter:
            raise MarkdownError("Markdown front matter is malformed")
        raise MarkdownError("Markdown front matter was not found")
    heading = _find_heading(document, fragment)
    return "".join(document.lines[heading.start : heading.end])


def section_listing(text: str, max_level: int = 3) -> dict[str, object]:
    if isinstance(max_level, bool) or not isinstance(max_level, int):
        raise MarkdownError("max_level must be an integer")
    if not 1 <= max_level <= 6:
        raise MarkdownError("max_level must be between 1 and 6")
    document = parse_markdown(text)
    return {
        "has_front_matter": document.front_matter is not None,
        "sections": [
            {
                "level": heading.level,
                "title": heading.title,
                "anchor": heading.anchor,
            }
            for heading in document.headings
            if heading.level <= max_level
        ],
    }


def _ends_in_newline(text: str) -> bool:
    return text.endswith(("\r", "\n"))


def _starts_with_newline(text: str) -> bool:
    return text.startswith(("\r", "\n"))


def overwrite_section(text: str, fragment: str, body: str) -> str:
    if not isinstance(body, str):
        raise MarkdownError("body must be a string")
    document = parse_markdown(text)
    heading = _find_heading(document, fragment)
    heading_end = document.offset(heading.heading_end)
    section_end = document.offset(heading.end)
    prefix = text[:heading_end]
    suffix = text[section_end:]
    replacement = body
    if (
        replacement
        and not _ends_in_newline(prefix)
        and not _starts_with_newline(replacement)
    ):
        replacement = newline_style(text) + replacement
    if suffix and replacement and not _ends_in_newline(replacement):
        replacement += newline_style(text)
    return prefix + replacement + suffix


def _validate_title(title: str) -> str:
    if not isinstance(title, str):
        raise MarkdownError("title must be a string")
    if any(char in title for char in "\r\n"):
        raise MarkdownError("title must be one line")
    normalized = title.strip(" \t")
    if not normalized or any(unicodedata.category(char) == "Cc" for char in normalized):
        raise MarkdownError("title must be non-empty")
    parsed = _ATX_HEADING.match(f"# {normalized}")
    assert parsed is not None
    semantic = re.sub(r"[ \t]+#+[ \t]*$", "", parsed.group(2) or "").strip(" \t")
    if not semantic:
        raise MarkdownError("title must be non-empty")
    return normalized


def append_section(
    text: str, fragment: str | None, title: str, body: str
) -> str:
    title = _validate_title(title)
    if not isinstance(body, str):
        raise MarkdownError("body must be a string")
    document = parse_markdown(text)
    if document.malformed_front_matter:
        raise MarkdownError("Markdown front matter is malformed")
    if fragment is None:
        level = 1
        insertion = len(text)
    else:
        parent = _find_heading(document, fragment)
        if parent.level == 6:
            raise MarkdownError("A level-6 section cannot have a subsection")
        level = parent.level + 1
        insertion = document.offset(parent.end)

    newline = newline_style(text)
    prefix = text[:insertion]
    suffix = text[insertion:]
    section = f"{'#' * level} {title}"
    if body:
        section += newline + body

    original_final_newline = _ends_in_newline(text)
    if prefix and not _ends_in_newline(prefix):
        section = newline + section
    if suffix and not _ends_in_newline(section):
        section += newline
    elif not suffix and original_final_newline and not _ends_in_newline(section):
        section += newline
    return prefix + section + suffix


def delete_section(text: str, fragment: str) -> str:
    document = parse_markdown(text)
    heading = _find_heading(document, fragment)
    return text[: document.offset(heading.start)] + text[document.offset(heading.end) :]


def _body_has_delimiter(body: str, delimiter: str) -> bool:
    return any(_logical_line(line) == delimiter for line in _split_lines(body))


def set_front_matter(text: str, body: str) -> str:
    if not isinstance(body, str):
        raise MarkdownError("body must be a string")
    document = parse_markdown(text)
    if document.malformed_front_matter:
        raise MarkdownError("Markdown front matter is malformed")

    current = document.front_matter
    delimiter = current.delimiter if current is not None else "---"
    if body and _body_has_delimiter(body, delimiter):
        raise MarkdownError("body contains the front-matter delimiter")

    if current is not None:
        end = document.offset(current.end)
        if not body:
            return text[end:]
        opener = document.lines[0]
        closer = document.lines[current.end - 1]
        newline = newline_style(text)
        if not _ends_in_newline(opener):
            opener += newline
        replacement = opener + body
        if not _ends_in_newline(replacement):
            replacement += newline
        return replacement + closer + text[end:]

    if not body:
        return text
    newline = newline_style(text)
    block = f"---{newline}{body}"
    if not _ends_in_newline(block):
        block += newline
    block += "---"
    if text:
        block += newline
    return block + text
