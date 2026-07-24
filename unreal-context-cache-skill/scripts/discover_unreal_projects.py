#!/usr/bin/env python3
"""Discover Unreal projects and classify their declared engine versions."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any


IGNORED_DIRECTORIES = {
    ".git",
    ".svn",
    "Binaries",
    "DerivedDataCache",
    "Intermediate",
    "Saved",
}

VERSION_PATTERN = re.compile(
    r"(?<!\d)(?P<major>\d+)\.(?P<minor>\d+)(?:\.(?P<patch>\d+))?"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Recursively find .uproject files and classify Unreal 5.8+ support."
    )
    parser.add_argument("workspace", type=Path, help="Workspace directory to scan")
    parser.add_argument(
        "--json",
        action="store_true",
        dest="as_json",
        help="Emit machine-readable JSON",
    )
    return parser.parse_args()


def declared_version(association: Any) -> tuple[int, int, int] | None:
    if not isinstance(association, str):
        return None
    match = VERSION_PATTERN.search(association)
    if not match:
        return None
    return (
        int(match.group("major")),
        int(match.group("minor")),
        int(match.group("patch") or 0),
    )


def status_for(version: tuple[int, int, int] | None) -> str:
    if version is None:
        return "unknown"
    return "supported" if version >= (5, 8, 0) else "unsupported"


def find_projects(workspace: Path) -> list[dict[str, Any]]:
    projects: list[dict[str, Any]] = []
    pending = [workspace]

    while pending:
        directory = pending.pop()
        try:
            children = sorted(directory.iterdir(), key=lambda path: path.name.casefold())
        except (OSError, PermissionError) as exc:
            print(f"warning: cannot read {directory}: {exc}", file=sys.stderr)
            continue

        for child in children:
            if child.is_dir():
                if child.name not in IGNORED_DIRECTORIES:
                    pending.append(child)
                continue
            if child.suffix.casefold() != ".uproject":
                continue

            association: Any = None
            parse_error: str | None = None
            try:
                payload = json.loads(child.read_text(encoding="utf-8-sig"))
                association = payload.get("EngineAssociation")
            except (OSError, UnicodeError, json.JSONDecodeError) as exc:
                parse_error = str(exc)

            version = declared_version(association)
            projects.append(
                {
                    "name": child.stem,
                    "uproject": str(child.resolve()),
                    "project_root": str(child.parent.resolve()),
                    "engine_association": association,
                    "declared_version": (
                        ".".join(str(part) for part in version) if version else None
                    ),
                    "status": status_for(version),
                    "parse_error": parse_error,
                }
            )

    return sorted(projects, key=lambda item: item["uproject"].casefold())


def main() -> int:
    args = parse_args()
    workspace = args.workspace.expanduser().resolve()
    if not workspace.is_dir():
        print(f"error: workspace is not a directory: {workspace}", file=sys.stderr)
        return 2

    projects = find_projects(workspace)
    if args.as_json:
        print(json.dumps({"workspace": str(workspace), "projects": projects}, indent=2))
    elif not projects:
        print(f"No Unreal projects found under {workspace}")
    else:
        for project in projects:
            association = project["engine_association"] or "not declared"
            print(
                f'{project["status"]:11} {project["name"]} '
                f'({association}) -> {project["uproject"]}'
            )
            if project["parse_error"]:
                print(f'  parse error: {project["parse_error"]}')
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
