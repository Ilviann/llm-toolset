"""Immutable startup configuration for Markdown MCP."""

from __future__ import annotations

import argparse
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence


class ConfigurationError(Exception):
    """A stable startup configuration error."""


@dataclass(frozen=True)
class Settings:
    """Validated effective server settings."""

    root: Path
    writable: bool = False

    @classmethod
    def for_root(
        cls, root: str | os.PathLike[str], *, writable: bool = False
    ) -> "Settings":
        if not isinstance(root, (str, os.PathLike)):
            raise ConfigurationError("Root must be a folder path")
        try:
            resolved = Path(root).expanduser().resolve(strict=True)
        except (OSError, RuntimeError, ValueError):
            raise ConfigurationError("Root folder does not exist") from None
        if not resolved.is_dir():
            raise ConfigurationError("Root must be an existing folder")
        return cls(root=resolved, writable=bool(writable))


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Root-confined Markdown file MCP server"
    )
    parser.add_argument("root", help="Folder containing existing Markdown files")
    parser.add_argument(
        "--writable",
        action="store_true",
        help="Enable section and front-matter editing tools",
    )
    return parser


def load_settings(argv: Sequence[str] | None = None) -> Settings:
    parser = create_parser()
    args = parser.parse_args(argv)
    try:
        return Settings.for_root(args.root, writable=args.writable)
    except ConfigurationError as exc:
        parser.error(str(exc))

