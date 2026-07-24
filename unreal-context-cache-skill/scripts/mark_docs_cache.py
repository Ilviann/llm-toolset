#!/usr/bin/env python3
"""Refresh docs/.cache.md with the current time and Git branch."""

from __future__ import annotations

import argparse
import subprocess
import sys
from datetime import datetime
from pathlib import Path


class CacheMarkerError(RuntimeError):
    """Raised for an actionable cache marker problem."""


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Refresh a Git-managed Unreal project's docs/.cache.md marker."
    )
    parser.add_argument("project_root", type=Path)
    return parser.parse_args()


def run_git(cwd: Path, *args: str) -> str:
    try:
        result = subprocess.run(
            ["git", "-C", str(cwd), *args],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
    except FileNotFoundError as exc:
        raise CacheMarkerError("Git executable was not found") from exc
    if result.returncode != 0:
        message = result.stderr.decode("utf-8", errors="replace").strip()
        raise CacheMarkerError(
            f'git {" ".join(args)} failed with exit code {result.returncode}: {message}'
        )
    return result.stdout.decode("utf-8", errors="replace").strip()


def main() -> int:
    args = parse_args()
    try:
        project_root = args.project_root.expanduser().resolve()
        if not project_root.is_dir():
            raise CacheMarkerError(f"project root is not a directory: {project_root}")
        if not list(project_root.glob("*.uproject")):
            raise CacheMarkerError(
                f"project root contains no .uproject file: {project_root}"
            )

        run_git(project_root, "rev-parse", "--show-toplevel")
        branch = run_git(project_root, "branch", "--show-current")
        if not branch:
            short_commit = run_git(project_root, "rev-parse", "--short", "HEAD")
            branch = f"DETACHED@{short_commit}"

        updated = datetime.now().astimezone().isoformat(timespec="microseconds")
        cache_file = project_root / "docs" / ".cache.md"
        cache_file.parent.mkdir(parents=True, exist_ok=True)
        content = (
            "# Documentation cache\n\n"
            f"- Updated: `{updated}`\n"
            f"- Branch: `{branch}`\n"
        )
        with cache_file.open("w", encoding="utf-8", newline="\n") as handle:
            handle.write(content)

        print(f"Updated documentation cache marker: {cache_file}")
        print(f"Time: {updated}")
        print(f"Branch: {branch}")
        return 0
    except (OSError, UnicodeError, CacheMarkerError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
