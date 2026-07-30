"""Bounded Markdown path fragments and exact source-section extraction."""

from __future__ import annotations

import html
import re
import unicodedata
from dataclasses import dataclass
from pathlib import Path
from urllib.parse import unquote_to_bytes


MARKDOWN_EXTENSIONS = frozenset({".md", ".markdown"})

_ATX_HEADING = re.compile(r"^ {0,3}(#{1,6})(?:[ \t]+(.*?)|[ \t]*)$")
_SETEXT_UNDERLINE = re.compile(r"^ {0,3}(=+|-+)[ \t]*$")
_FENCE_OPEN = re.compile(r"^ {0,3}(`{3,}|~{3,})(.*)$")
_LINK = re.compile(r"!?\[([^\]]*)\]\([^)]*\)")
_HTML_TAG = re.compile(r"<[^>\n]*>")
_ESCAPE = re.compile(r"\\([!\"#$%&'()*+,\-./:;<=>?@\[\\\]^_`{|}~])")
_PERCENT_ESCAPE = re.compile(r"%(?![0-9A-Fa-f]{2})")


class MarkdownReadError(Exception):
    """A stable Markdown selection error safe to return through MCP."""


@dataclass(frozen=True)
class _Heading:
    level: int
    start: int
    title: str
    anchor: str = ""


def is_markdown_path(path: str | Path) -> bool:
    """Return whether the final suffix is a supported Markdown extension."""

    return Path(path).suffix.casefold() in MARKDOWN_EXTENSIONS


def split_markdown_fragment(user_path: str) -> tuple[str, str | None]:
    """Split the last supported raw Markdown fragment from a model-facing path."""

    path, separator, fragment = user_path.rpartition("#")
    if not separator or not is_markdown_path(path):
        return user_path, None
    return path, fragment


def decode_markdown_fragment(fragment: str) -> str:
    """Validate and decode a raw fragment after filesystem/text validation."""

    if not fragment:
        raise MarkdownReadError("Markdown anchor is empty")
    if _PERCENT_ESCAPE.search(fragment):
        raise MarkdownReadError("Markdown anchor is malformed")
    try:
        decoded = unquote_to_bytes(fragment).decode("utf-8")
    except (UnicodeDecodeError, UnicodeEncodeError):
        raise MarkdownReadError("Markdown anchor is malformed") from None
    if (
        not decoded
        or any(
            char.isspace() or unicodedata.category(char) in {"Cc", "Cf"}
            for char in decoded
        )
        or any(char in "/\\#" for char in decoded)
    ):
        raise MarkdownReadError("Markdown anchor is malformed")
    return decoded


def _logical_line(line: str) -> str:
    if line.endswith("\r\n"):
        return line[:-2]
    if line.endswith(("\r", "\n")):
        return line[:-1]
    return line


def _front_matter_end(lines: list[str]) -> int | None:
    if not lines or _logical_line(lines[0]) != "---":
        return None
    for index in range(1, len(lines)):
        if _logical_line(lines[index]) in {"---", "..."}:
            return index + 1
    return len(lines)


def _front_matter(lines: list[str]) -> str:
    if not lines or _logical_line(lines[0]) != "---":
        raise MarkdownReadError("Markdown front matter was not found")
    for index in range(1, len(lines)):
        if _logical_line(lines[index]) in {"---", "..."}:
            return "".join(lines[: index + 1])
    raise MarkdownReadError("Markdown front matter was not found")


def _visible_heading_text(title: str) -> str:
    """Approximate GitHub-rendered heading text without a Markdown dependency."""

    title = _LINK.sub(lambda match: match.group(1), title)
    title = _HTML_TAG.sub("", title)
    title = _ESCAPE.sub(lambda match: match.group(1), title)
    title = title.replace("`", "")
    return " ".join(html.unescape(title).split())


def _base_anchor(title: str) -> str:
    output: list[str] = []
    for char in _visible_heading_text(title).lower():
        if char == " ":
            output.append("-")
        elif char in "-_":
            output.append(char)
        elif unicodedata.category(char).startswith("P"):
            continue
        elif unicodedata.category(char) in {"Cc", "Cf"}:
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


def _headings(lines: list[str], content_start: int) -> list[_Heading]:
    headings: list[_Heading] = []
    fence_character: str | None = None
    fence_length = 0
    previous_plain: tuple[int, str] | None = None

    for index in range(content_start, len(lines)):
        logical = _logical_line(lines[index])

        if fence_character is not None:
            closing = re.fullmatch(
                rf" {{0,3}}{re.escape(fence_character)}{{{fence_length},}}[ \t]*",
                logical,
            )
            if closing:
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
            headings.append(_Heading(len(atx.group(1)), index, title))
            previous_plain = None
            continue

        setext = _SETEXT_UNDERLINE.match(logical)
        if setext and previous_plain is not None:
            previous_index, title = previous_plain
            headings.append(
                _Heading(1 if setext.group(1)[0] == "=" else 2, previous_index, title)
            )
            previous_plain = None
            continue

        if logical.strip():
            leading = len(logical) - len(logical.lstrip(" "))
            previous_plain = (
                (index, logical[leading:]) if leading <= 3 else None
            )
        else:
            previous_plain = None

    used: set[str] = set()
    next_suffix: dict[str, int] = {}
    anchored: list[_Heading] = []
    for heading in headings:
        base = _base_anchor(heading.title)
        candidate = base
        suffix = next_suffix.get(base, 0)
        while candidate in used:
            suffix += 1
            candidate = f"{base}-{suffix}"
        next_suffix[base] = suffix
        used.add(candidate)
        anchored.append(
            _Heading(heading.level, heading.start, heading.title, candidate)
        )
    return anchored


def extract_markdown(text: str, fragment: str) -> str:
    """Return exact source for YAML front matter or one generated heading anchor."""

    lines = text.splitlines(keepends=True)
    if fragment == "---":
        return _front_matter(lines)

    front_matter_end = _front_matter_end(lines)
    content_start = front_matter_end or 0
    headings = _headings(lines, content_start)
    matches = [
        index
        for index, heading in enumerate(headings)
        if heading.anchor == fragment
    ]
    if not matches:
        raise MarkdownReadError("Markdown anchor was not found")
    if len(matches) > 1:
        raise MarkdownReadError("Markdown anchor is ambiguous")

    selected_index = matches[0]
    selected = headings[selected_index]
    end = len(lines)
    for following in headings[selected_index + 1 :]:
        if following.level <= selected.level:
            end = following.start
            break
    return "".join(lines[selected.start:end])
