#!/usr/bin/env python3
"""Collect Git changes relevant to one documented Unreal project."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path, PurePosixPath
from typing import Any


STATE_BLOCK_PATTERN = re.compile(
    r"<!--\s*unreal-context-cache-state\s*\n(?P<body>.*?)\n-->",
    re.DOTALL,
)

SOURCE_SUFFIXES = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".inl",
    ".ipp",
}
BUILD_SUFFIXES = {".cs", ".py", ".ps1", ".bat", ".cmd", ".sh"}
UNREAL_BINARY_SUFFIXES = {".uasset", ".umap"}


class ReconciliationError(RuntimeError):
    """Raised for an actionable repository or state problem."""


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Compare a documented source checkpoint with a target commit and report "
            "committed plus local changes for one Unreal project."
        )
    )
    parser.add_argument("project_root", type=Path)
    parser.add_argument("--from", dest="base", help="Base commit; defaults to project state")
    parser.add_argument("--to", dest="target", default="HEAD", help="Target commit")
    parser.add_argument(
        "--add-tracked-path",
        action="append",
        default=[],
        help="Temporarily add a repository-relative tracked path",
    )
    parser.add_argument("--json", action="store_true", dest="as_json")
    return parser.parse_args()


def run_git(
    cwd: Path,
    *args: str,
    binary: bool = False,
    check: bool = True,
) -> str | bytes | subprocess.CompletedProcess[bytes]:
    try:
        result = subprocess.run(
            ["git", "-C", str(cwd), *args],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
    except FileNotFoundError as exc:
        raise ReconciliationError("Git executable was not found") from exc

    if check and result.returncode != 0:
        message = result.stderr.decode("utf-8", errors="replace").strip()
        raise ReconciliationError(
            f'git {" ".join(args)} failed with exit code {result.returncode}: {message}'
        )
    if not check:
        return result
    if binary:
        return result.stdout
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
        raise ReconciliationError(f"tracked path must be repository-relative: {value}")
    return path.as_posix()


def parse_state(state_file: Path) -> tuple[str | None, list[str]]:
    if not state_file.is_file():
        return None, []
    text = state_file.read_text(encoding="utf-8-sig").replace("\r\n", "\n")
    matches = list(STATE_BLOCK_PATTERN.finditer(text))
    if len(matches) != 1:
        raise ReconciliationError(
            f"{state_file} must contain exactly one unreal-context-cache-state marker"
        )

    checkpoint: str | None = None
    tracked_paths: list[str] = []
    schema: str | None = None
    for raw_line in matches[0].group("body").splitlines():
        line = raw_line.strip()
        if not line or ":" not in line:
            continue
        key, value = (part.strip() for part in line.split(":", 1))
        if key == "schema":
            schema = value
        elif key == "last-reconciled-source-commit":
            checkpoint = value
        elif key == "tracked-path":
            tracked_paths.append(normalize_repo_path(value))
    if schema != "1":
        raise ReconciliationError(f"{state_file} uses an unsupported state schema")
    return checkpoint, tracked_paths


def resolve_commit(repository: Path, revision: str) -> str:
    return str(run_git(repository, "rev-parse", "--verify", f"{revision}^{{commit}}"))


def parse_name_status(payload: bytes) -> list[dict[str, str]]:
    tokens = payload.decode("utf-8", errors="surrogateescape").split("\0")
    if tokens and tokens[-1] == "":
        tokens.pop()
    changes: list[dict[str, str]] = []
    index = 0
    while index < len(tokens):
        status = tokens[index]
        index += 1
        if not status:
            continue
        if status[0] in {"R", "C"}:
            if index + 1 >= len(tokens):
                raise ReconciliationError("unexpected truncated Git rename/copy record")
            old_path, path = tokens[index], tokens[index + 1]
            index += 2
            changes.append({"status": status, "old_path": old_path, "path": path})
        else:
            if index >= len(tokens):
                raise ReconciliationError("unexpected truncated Git change record")
            path = tokens[index]
            index += 1
            changes.append({"status": status, "path": path})
    return changes


def classify_path(path_value: str) -> str:
    path = PurePosixPath(path_value)
    lowered_parts = {part.casefold() for part in path.parts}
    suffix = path.suffix.casefold()
    name = path.name.casefold()

    if "docs" in lowered_parts and suffix == ".md":
        return "documentation"
    if suffix in UNREAL_BINARY_SUFFIXES:
        return "unreal-binary-asset"
    if suffix in SOURCE_SUFFIXES:
        return "source"
    if suffix == ".ini":
        return "configuration"
    if suffix in {".uproject", ".uplugin"}:
        return "unreal-descriptor"
    if name.endswith(".build.cs") or name.endswith(".target.cs") or suffix in BUILD_SUFFIXES:
        return "build-or-tooling"
    return "other"


def classify_changes(changes: list[dict[str, str]]) -> list[dict[str, str]]:
    classified: list[dict[str, str]] = []
    for change in changes:
        item = dict(change)
        item["category"] = classify_path(change["path"])
        classified.append(item)
    return classified


def path_in_scope(path_value: str, tracked_paths: list[str]) -> bool:
    normalized = path_value.replace("\\", "/").rstrip("/")
    return any(
        tracked == "."
        or normalized == tracked
        or normalized.startswith(f"{tracked}/")
        for tracked in tracked_paths
    )


def change_in_scope(change: dict[str, str], tracked_paths: list[str]) -> bool:
    return path_in_scope(change["path"], tracked_paths) or (
        "old_path" in change and path_in_scope(change["old_path"], tracked_paths)
    )


def diff_changes(
    repository: Path,
    base: str | None,
    target: str | None,
    tracked_paths: list[str],
    cached: bool = False,
) -> list[dict[str, str]]:
    args = ["diff", "--name-status", "-z", "--find-renames"]
    if cached:
        args.append("--cached")
    if base is not None:
        args.extend([base, target or "HEAD"])
    args.append("--")
    args.extend(tracked_paths)
    payload = run_git(repository, *args, binary=True)
    assert isinstance(payload, bytes)
    return classify_changes(parse_name_status(payload))


def untracked_changes(repository: Path, tracked_paths: list[str]) -> list[dict[str, str]]:
    args = ["ls-files", "--others", "--exclude-standard", "-z", "--", *tracked_paths]
    payload = run_git(repository, *args, binary=True)
    assert isinstance(payload, bytes)
    paths = payload.decode("utf-8", errors="surrogateescape").split("\0")
    return [
        {"status": "??", "path": path, "category": classify_path(path)}
        for path in paths
        if path
    ]


def summarize(changes: list[dict[str, str]]) -> dict[str, int]:
    return dict(sorted(Counter(change["category"] for change in changes).items()))


def collect(args: argparse.Namespace) -> dict[str, Any]:
    project_root = args.project_root.expanduser().resolve()
    if not project_root.is_dir():
        raise ReconciliationError(f"project root is not a directory: {project_root}")
    if not list(project_root.glob("*.uproject")):
        raise ReconciliationError(f"project root contains no .uproject file: {project_root}")

    repository = Path(str(run_git(project_root, "rev-parse", "--show-toplevel"))).resolve()
    try:
        project_path = project_root.relative_to(repository).as_posix() or "."
    except ValueError as exc:
        raise ReconciliationError("project root is not inside the resolved Git repository") from exc

    state_file = project_root / "docs" / "reconciliation-state.md"
    state_checkpoint, state_paths = parse_state(state_file)
    base_input = args.base or state_checkpoint
    if not base_input:
        raise ReconciliationError(
            "no base commit supplied and no documented checkpoint exists; "
            "perform initial reconciliation or pass --from"
        )

    base = resolve_commit(repository, base_input)
    target = resolve_commit(repository, args.target)

    tracked_paths: list[str] = []
    for candidate in [project_path, *state_paths, *args.add_tracked_path]:
        normalized = normalize_repo_path(candidate)
        if normalized not in tracked_paths:
            tracked_paths.append(normalized)

    all_payload = run_git(
        repository,
        "diff",
        "--name-status",
        "-z",
        "--find-renames",
        base,
        target,
        "--",
        binary=True,
    )
    assert isinstance(all_payload, bytes)
    all_committed = classify_changes(parse_name_status(all_payload))
    committed = [
        change for change in all_committed if change_in_scope(change, tracked_paths)
    ]
    outside_scope = [
        change for change in all_committed if not change_in_scope(change, tracked_paths)
    ]

    staged = diff_changes(repository, None, None, tracked_paths, cached=True)
    unstaged = diff_changes(repository, None, None, tracked_paths)
    untracked = untracked_changes(repository, tracked_paths)

    ancestor_result = run_git(
        repository,
        "merge-base",
        "--is-ancestor",
        base,
        target,
        check=False,
    )
    assert isinstance(ancestor_result, subprocess.CompletedProcess)
    base_is_ancestor = ancestor_result.returncode == 0

    log_text = str(
        run_git(
            repository,
            "log",
            "--reverse",
            "--format=%H%x09%s",
            f"{base}..{target}",
        )
    )
    commits = []
    for line in log_text.splitlines():
        commit, _, subject = line.partition("\t")
        if commit:
            commits.append({"commit": commit, "subject": subject})

    warnings: list[str] = []
    if not base_is_ancestor:
        warnings.append(
            "The base is not an ancestor of the target. Direct tree comparison is still "
            "used; review removals and branch-specific replacements carefully."
        )
    if any(change["category"] == "unreal-binary-asset" for change in committed):
        warnings.append(
            "Committed Unreal binary assets changed. Use targeted editor or asset-registry "
            "inspection before documenting semantics."
        )
    if outside_scope:
        warnings.append(
            "Committed paths changed outside tracked scope. Review them for shared project "
            "dependencies before deciding they are irrelevant."
        )
    local_changes = [*staged, *unstaged, *untracked]
    if any(change["category"] != "documentation" for change in local_changes):
        warnings.append(
            "Local non-documentation changes are reported separately and are not eligible "
            "for checkpoint advancement until committed."
        )
    elif local_changes:
        warnings.append(
            "Local documentation changes are present. Validate them before advancing or "
            "committing the reconciliation state."
        )

    return {
        "project_root": str(project_root),
        "repository_root": str(repository),
        "project_path": project_path,
        "state_file": str(state_file),
        "base": base,
        "target": target,
        "base_is_ancestor": base_is_ancestor,
        "tracked_paths": tracked_paths,
        "commits": commits,
        "changes": {
            "committed_in_scope": committed,
            "committed_outside_scope": outside_scope,
            "staged_in_scope": staged,
            "unstaged_in_scope": unstaged,
            "untracked_in_scope": untracked,
        },
        "summary": {
            "committed_in_scope": summarize(committed),
            "committed_outside_scope": summarize(outside_scope),
            "staged_in_scope": summarize(staged),
            "unstaged_in_scope": summarize(unstaged),
            "untracked_in_scope": summarize(untracked),
        },
        "warnings": warnings,
        "checkpoint_candidate": target,
    }


def print_human(report: dict[str, Any]) -> None:
    print(f'Project: {report["project_root"]}')
    print(f'Repository: {report["repository_root"]}')
    print(f'Range: {report["base"]}..{report["target"]}')
    print(f'Base is ancestor: {str(report["base_is_ancestor"]).lower()}')
    print(f'Tracked paths: {", ".join(report["tracked_paths"])}')
    for group, changes in report["changes"].items():
        print(f"\n{group} ({len(changes)}):")
        if not changes:
            print("  None")
            continue
        for change in changes:
            rename = (
                f'{change["old_path"]} -> {change["path"]}'
                if "old_path" in change
                else change["path"]
            )
            print(f'  {change["status"]:5} {change["category"]:20} {rename}')
    if report["warnings"]:
        print("\nWarnings:")
        for warning in report["warnings"]:
            print(f"  - {warning}")
    print(f'\nCheckpoint candidate: {report["checkpoint_candidate"]}')


def main() -> int:
    args = parse_args()
    try:
        report = collect(args)
    except (OSError, UnicodeError, ReconciliationError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    if args.as_json:
        print(json.dumps(report, indent=2, ensure_ascii=False))
    else:
        print_human(report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
