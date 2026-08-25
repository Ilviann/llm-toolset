"""Authoritative Markdown path and fragment resolution."""

from __future__ import annotations

import os
import stat
from dataclasses import dataclass
from pathlib import Path


MARKDOWN_EXTENSIONS = frozenset({".md", ".markdown"})
MAX_PATH_CHARS = 4096


class PathAccessError(Exception):
    """A stable, non-sensitive model-facing path error."""


def is_markdown_path(path: str | os.PathLike[str]) -> bool:
    try:
        return Path(path).suffix.casefold() in MARKDOWN_EXTENSIONS
    except (OSError, RuntimeError, ValueError, TypeError):
        return False


def split_markdown_fragment(user_path: str) -> tuple[str, str | None]:
    """Split only a final fragment whose preceding path is Markdown."""

    path, separator, fragment = user_path.rpartition("#")
    if separator and is_markdown_path(path):
        return path, fragment
    return user_path, None


@dataclass(frozen=True)
class ResolvedMarkdownPath:
    """An authorized existing Markdown file and optional raw selector."""

    model_path: str
    plain_path: str
    path: Path
    fragment: str | None


class MarkdownPathResolver:
    """Resolve model-facing paths without weakening native path semantics."""

    def __init__(self, root: Path) -> None:
        try:
            self.root = root.resolve(strict=True)
        except (OSError, RuntimeError, ValueError):
            raise PathAccessError("Root folder is unavailable") from None

    def resolve(
        self, model_path: str, *, allow_fragment: bool = True
    ) -> ResolvedMarkdownPath:
        if not isinstance(model_path, str):
            raise PathAccessError("Path must be a string")
        if not model_path or "\x00" in model_path or len(model_path) > MAX_PATH_CHARS:
            raise PathAccessError("Invalid path")

        plain_path, fragment = split_markdown_fragment(model_path)
        if fragment is not None and not allow_fragment:
            raise PathAccessError("Path fragments are not allowed for this tool")
        if not is_markdown_path(plain_path):
            raise PathAccessError("Only .md and .markdown files are supported")

        try:
            requested = Path(plain_path)
            candidate = requested if requested.is_absolute() else self.root / requested
            resolved = candidate.resolve(strict=True)
            resolved.relative_to(self.root)
        except (OSError, RuntimeError, ValueError):
            raise PathAccessError("Path is outside root or does not exist") from None

        if not is_markdown_path(resolved):
            raise PathAccessError("Only .md and .markdown files are supported")
        try:
            mode = resolved.stat().st_mode
        except OSError:
            raise PathAccessError("Path is outside root or does not exist") from None
        if not stat.S_ISREG(mode):
            raise PathAccessError("Path is not a regular file")

        return ResolvedMarkdownPath(model_path, plain_path, resolved, fragment)

    def revalidate(self, plain_path: str, expected: Path) -> Path:
        """Resolve an existing target again immediately before replacement."""

        current = self.resolve(plain_path, allow_fragment=False).path
        if current != expected:
            raise PathAccessError("File path changed during edit")
        try:
            parent = current.parent.resolve(strict=True)
            parent.relative_to(self.root)
        except (OSError, RuntimeError, ValueError):
            raise PathAccessError("File path changed during edit") from None
        if parent != expected.parent:
            raise PathAccessError("File path changed during edit")
        return current
