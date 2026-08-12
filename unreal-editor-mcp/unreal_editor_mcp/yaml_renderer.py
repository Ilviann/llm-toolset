"""Dependency-free deterministic safe YAML for model-facing asset inspection."""

from __future__ import annotations

import math
from typing import Any


def render_safe_yaml(value: Any) -> str:
    """Render JSON-shaped data as a deterministic YAML 1.2-compatible subset."""
    _validate(value, depth=0)
    lines = _render(value, indent=0)
    return "\n".join(lines) + "\n"


def _validate(value: Any, *, depth: int) -> None:
    if depth > 32:
        raise ValueError("YAML value exceeds the supported depth")
    if value is None or type(value) in {bool, int, str}:
        return
    if type(value) is float:
        if not math.isfinite(value):
            raise ValueError("YAML numbers must be finite")
        return
    if type(value) is list:
        for item in value:
            _validate(item, depth=depth + 1)
        return
    if type(value) is dict:
        for key, item in value.items():
            if type(key) is not str:
                raise ValueError("YAML mapping keys must be strings")
            _validate(item, depth=depth + 1)
        return
    raise ValueError("YAML renderer accepts only JSON-shaped values")


def _quoted(value: str) -> str:
    escaped: list[str] = []
    for character in value:
        codepoint = ord(character)
        if character == '"':
            escaped.append('\\"')
        elif character == "\\":
            escaped.append("\\\\")
        elif character == "\n":
            escaped.append("\\n")
        elif character == "\r":
            escaped.append("\\r")
        elif character == "\t":
            escaped.append("\\t")
        elif codepoint < 0x20 or codepoint == 0x7F:
            escaped.append(f"\\u{codepoint:04x}")
        elif 0xD800 <= codepoint <= 0xDFFF:
            raise ValueError("YAML strings cannot contain surrogate code points")
        else:
            escaped.append(character)
    return '"' + "".join(escaped) + '"'


def _scalar(value: Any) -> str:
    if value is None:
        return "null"
    if type(value) is bool:
        return "true" if value else "false"
    if type(value) is int:
        return str(value)
    if type(value) is float:
        if value == 0.0:
            return "0.0"
        return repr(value)
    if type(value) is str:
        return _quoted(value)
    raise TypeError("not a scalar")


def _render(value: Any, *, indent: int) -> list[str]:
    prefix = " " * indent
    if not isinstance(value, (dict, list)):
        return [prefix + _scalar(value)]
    if type(value) is dict:
        if not value:
            return [prefix + "{}"]
        lines: list[str] = []
        for key in sorted(value):
            item = value[key]
            encoded_key = _quoted(key)
            if isinstance(item, (dict, list)) and item:
                lines.append(prefix + encoded_key + ":")
                lines.extend(_render(item, indent=indent + 2))
            else:
                lines.append(prefix + encoded_key + ": " + (
                    "{}" if type(item) is dict else "[]" if type(item) is list else _scalar(item)
                ))
        return lines
    if not value:
        return [prefix + "[]"]
    lines = []
    for item in value:
        if isinstance(item, (dict, list)) and item:
            lines.append(prefix + "-")
            lines.extend(_render(item, indent=indent + 2))
        else:
            lines.append(prefix + "- " + (
                "{}" if type(item) is dict else "[]" if type(item) is list else _scalar(item)
            ))
    return lines
