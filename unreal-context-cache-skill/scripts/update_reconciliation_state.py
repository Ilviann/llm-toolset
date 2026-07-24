#!/usr/bin/env python3
"""Initialize or advance a project's Markdown Git reconciliation checkpoint."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath


STATE_BLOCK_PATTERN = re.compile(
    r"<!--\s*unreal-context-cache-state\s*\n(?P<body>.*?)\n-->",
    re.DOTALL,
)


class StateUpdateError(RuntimeError):
    """Raised for an actionable state update problem."""


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Initialize or advance docs/reconciliation-state.md."
    )
    parser.add_argument("project_root", type=Path)
    parser.add_argument("--commit", required=True, help="Reconciled source commit")
    parser.add_argument(
        "--initialize",
        action="store_true",
        help="Create a new state document; fail if it already exists",
    )
    parser.add_argument(
        "--add-tracked-path",
        action="append",
        default=[],
        help="Add a repository-relative path that can affect this project",
    )
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
        raise StateUpdateError("Git executable was not found") from exc
    if result.returncode != 0:
        message = result.stderr.decode("utf-8", errors="replace").strip()
        raise StateUpdateError(
            f'git {" ".join(args)} failed with exit code {result.returncode}: {message}'
        )
    return result.stdout.decode("utf-8", errors="replace").strip()


def normalize_repo_path(value: str) -> str:
    normalized = value.strip().replace("\\", "/").rstrip("/")
    if normalized in ("", "."):
        return "."
    path = PurePosixPath(normalized)
    if (
        path.is_absolute()
        or re.match(r"^[A-Za-z]:", normalized)
        or ".." in path.parts
    ):
        raise StateUpdateError(f"tracked path must be repository-relative: {value}")
    return path.as_posix()


def existing_tracked_paths(text: str) -> list[str]:
    matches = list(STATE_BLOCK_PATTERN.finditer(text.replace("\r\n", "\n")))
    if len(matches) != 1:
        raise StateUpdateError(
            "existing state must contain exactly one unreal-context-cache-state marker"
        )
    paths: list[str] = []
    schema: str | None = None
    for raw_line in matches[0].group("body").splitlines():
        line = raw_line.strip()
        if line.startswith("schema:"):
            schema = line.split(":", 1)[1].strip()
        elif line.startswith("tracked-path:"):
            paths.append(normalize_repo_path(line.split(":", 1)[1]))
    if schema != "1":
        raise StateUpdateError("existing state uses an unsupported schema")
    return paths


def marker(commit: str, tracked_paths: list[str]) -> str:
    path_lines = "\n".join(f"tracked-path: {path}" for path in tracked_paths)
    return (
        "<!-- unreal-context-cache-state\n"
        "schema: 1\n"
        f"last-reconciled-source-commit: {commit}\n"
        f"{path_lines}\n"
        "-->"
    )


def nul_paths(payload: str) -> list[str]:
    return [path for path in payload.split("\0") if path]


def uncommitted_non_docs(
    repository: Path,
    tracked_paths: list[str],
    project_docs_path: str,
) -> list[str]:
    commands = (
        ("diff", "--name-only", "-z", "--", *tracked_paths),
        ("diff", "--cached", "--name-only", "-z", "--", *tracked_paths),
        (
            "ls-files",
            "--others",
            "--exclude-standard",
            "-z",
            "--",
            *tracked_paths,
        ),
    )
    changed_paths: list[str] = []
    for command in commands:
        for path in nul_paths(run_git(repository, *command)):
            normalized = path.replace("\\", "/")
            if (
                normalized != project_docs_path
                and not normalized.startswith(f"{project_docs_path}/")
                and normalized not in changed_paths
            ):
                changed_paths.append(normalized)
    return changed_paths


def main() -> int:
    args = parse_args()
    try:
        project_root = args.project_root.expanduser().resolve()
        if not project_root.is_dir():
            raise StateUpdateError(f"project root is not a directory: {project_root}")
        if not list(project_root.glob("*.uproject")):
            raise StateUpdateError(
                f"project root contains no .uproject file: {project_root}"
            )

        repository = Path(run_git(project_root, "rev-parse", "--show-toplevel")).resolve()
        try:
            project_path = project_root.relative_to(repository).as_posix() or "."
        except ValueError as exc:
            raise StateUpdateError(
                "project root is not inside the resolved Git repository"
            ) from exc

        commit = run_git(
            repository, "rev-parse", "--verify", f"{args.commit}^{{commit}}"
        )
        state_file = project_root / "docs" / "reconciliation-state.md"

        if args.initialize:
            if state_file.exists():
                raise StateUpdateError(
                    f"state already exists; omit --initialize to advance it: {state_file}"
                )
            tracked_paths = [normalize_repo_path(project_path)]
            existing_text = ""
        else:
            if not state_file.is_file():
                raise StateUpdateError(
                    f"state does not exist; use --initialize after initial reconciliation: "
                    f"{state_file}"
                )
            existing_text = state_file.read_text(encoding="utf-8-sig")
            tracked_paths = existing_tracked_paths(existing_text)

        for candidate in [project_path, *args.add_tracked_path]:
            normalized = normalize_repo_path(candidate)
            if normalized not in tracked_paths:
                tracked_paths.append(normalized)

        docs_path = "docs" if project_path == "." else f"{project_path}/docs"
        dirty_source_paths = uncommitted_non_docs(
            repository, tracked_paths, docs_path
        )
        if dirty_source_paths:
            rendered = ", ".join(dirty_source_paths[:10])
            if len(dirty_source_paths) > 10:
                rendered += f", and {len(dirty_source_paths) - 10} more"
            raise StateUpdateError(
                "refusing to advance while tracked non-documentation changes are "
                f"uncommitted: {rendered}"
            )

        updated_marker = marker(commit, tracked_paths)
        if args.initialize:
            state_file.parent.mkdir(parents=True, exist_ok=True)
            updated_text = (
                "# Documentation reconciliation state\n\n"
                f"{updated_marker}\n\n"
                "The checkpoint identifies the committed source tree against which this "
                "project's documentation was last reconciled. Tracked paths are relative "
                "to the Git repository root.\n"
            )
        else:
            updated_text = STATE_BLOCK_PATTERN.sub(updated_marker, existing_text, count=1)

        with state_file.open("w", encoding="utf-8", newline="\n") as handle:
            handle.write(updated_text)
        print(f"Updated reconciliation checkpoint: {state_file}")
        print(f"Source commit: {commit}")
        print(f'Tracked paths: {", ".join(tracked_paths)}')
        return 0
    except (OSError, UnicodeError, StateUpdateError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
