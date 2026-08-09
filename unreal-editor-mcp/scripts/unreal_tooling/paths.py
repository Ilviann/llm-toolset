"""Small bounded filesystem and JSON primitives shared by support tools."""

from __future__ import annotations

import json
from pathlib import Path

from .errors import ToolingError


_WINDOWS_REPARSE_POINT = 0x0400


def resolved(path: Path) -> Path:
    return path.expanduser().resolve()


def is_within(path: Path, directory: Path) -> bool:
    try:
        path.relative_to(directory)
    except ValueError:
        return False
    return True


def is_reparse_point(path: Path) -> bool:
    try:
        return bool(path.lstat().st_file_attributes & _WINDOWS_REPARSE_POINT)
    except (AttributeError, OSError):
        return path.is_symlink()


def read_json_object(path: Path, *, label: str, maximum_bytes: int) -> dict[str, object]:
    if type(maximum_bytes) is not int or maximum_bytes <= 0:
        raise ValueError("maximum_bytes must be a positive integer")
    try:
        with path.open("rb") as stream:
            data = stream.read(maximum_bytes + 1)
        if len(data) > maximum_bytes:
            raise ToolingError(f"{label} is larger than {maximum_bytes} bytes: {path}")
        value = json.loads(data.decode("utf-8-sig"))
    except ToolingError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ToolingError(f"{label} is not readable JSON: {path}: {error}") from error
    if not isinstance(value, dict):
        raise ToolingError(f"{label} must contain one JSON object: {path}")
    return value
