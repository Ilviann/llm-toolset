#!/usr/bin/env python3
"""Audit the indexed Markdown documentation tree of one Unreal project."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from urllib.parse import unquote


REQUIRED_DIRECTORIES = (
    "gd",
    "components",
    "types",
)
LINK_PATTERN = re.compile(r"\[[^\]]+\]\(([^)]+)\)")
STATE_BLOCK_PATTERN = re.compile(
    r"<!--\s*unreal-context-cache-state\s*\n(?P<body>.*?)\n-->",
    re.DOTALL,
)
FULL_OBJECT_ID_PATTERN = re.compile(
    r"^(?:[0-9a-fA-F]{40}|[0-9a-fA-F]{64})$"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Check docs/index.md coverage and the required Unreal knowledge folders."
    )
    parser.add_argument("project_root", type=Path, help="Directory containing a .uproject")
    parser.add_argument("--json", action="store_true", dest="as_json")
    parser.add_argument(
        "--require-git-state",
        action="store_true",
        help="Require and validate docs/reconciliation-state.md",
    )
    return parser.parse_args()


def inventory_section(index_text: str, heading: str) -> str | None:
    pattern = re.compile(
        rf"^##[ \t]+{re.escape(heading)}[ \t]*\n(.*?)(?=^##[ \t]+|\Z)",
        re.DOTALL | re.MULTILINE | re.IGNORECASE,
    )
    match = pattern.search(index_text)
    return match.group(1) if match else None


def inventory_entries(section: str) -> dict[str, bool]:
    entries: dict[str, bool] = {}
    for line in section.splitlines():
        match = LINK_PATTERN.search(line)
        if not match:
            continue
        target = unquote(match.group(1).split("#", 1)[0].strip()).replace("\\", "/")
        if target.startswith("./"):
            target = target[2:]
        target = target.rstrip("/")
        remainder = line[match.end() :].strip()
        entries[target] = bool(remainder and remainder.lstrip("—–-:").strip())
    return entries


def state_fields(state_text: str) -> tuple[dict[str, str], list[str]] | None:
    matches = list(STATE_BLOCK_PATTERN.finditer(state_text.replace("\r\n", "\n")))
    if len(matches) != 1:
        return None

    fields: dict[str, str] = {}
    tracked_paths: list[str] = []
    for raw_line in matches[0].group("body").splitlines():
        line = raw_line.strip()
        if not line or ":" not in line:
            continue
        key, value = (part.strip() for part in line.split(":", 1))
        if key == "tracked-path":
            tracked_paths.append(value)
        else:
            fields[key] = value
    return fields, tracked_paths


def audit_state_file(state_file: Path) -> list[dict[str, str]]:
    issues: list[dict[str, str]] = []
    try:
        state_text = state_file.read_text(encoding="utf-8-sig")
    except (OSError, UnicodeError) as exc:
        return [
            {
                "severity": "error",
                "path": str(state_file),
                "message": f"cannot read reconciliation state: {exc}",
            }
        ]

    parsed = state_fields(state_text)
    if parsed is None:
        return [
            {
                "severity": "error",
                "path": str(state_file),
                "message": "expected exactly one unreal-context-cache-state marker",
            }
        ]

    fields, tracked_paths = parsed
    if fields.get("schema") != "1":
        issues.append(
            {
                "severity": "error",
                "path": str(state_file),
                "message": "state marker schema must be 1",
            }
        )

    checkpoint = fields.get("last-reconciled-source-commit", "")
    if not FULL_OBJECT_ID_PATTERN.fullmatch(checkpoint):
        issues.append(
            {
                "severity": "error",
                "path": str(state_file),
                "message": "last-reconciled-source-commit must be a full Git object ID",
            }
        )

    if not tracked_paths:
        issues.append(
            {
                "severity": "error",
                "path": str(state_file),
                "message": "state marker must contain at least one tracked-path",
            }
        )

    for tracked_path in tracked_paths:
        normalized = tracked_path.replace("\\", "/")
        parts = [part for part in normalized.split("/") if part not in ("", ".")]
        if (
            normalized.startswith("/")
            or re.match(r"^[A-Za-z]:", normalized)
            or ".." in parts
        ):
            issues.append(
                {
                    "severity": "error",
                    "path": str(state_file),
                    "message": f"tracked-path must be repository-relative: {tracked_path}",
                }
            )
    return issues


def audit(project_root: Path, require_git_state: bool = False) -> list[dict[str, str]]:
    issues: list[dict[str, str]] = []
    docs = project_root / "docs"

    uprojects = sorted(project_root.glob("*.uproject"))
    if not uprojects:
        issues.append(
            {
                "severity": "error",
                "path": str(project_root),
                "message": "project root contains no .uproject file",
            }
        )

    if not docs.is_dir():
        issues.append(
            {
                "severity": "error",
                "path": str(docs),
                "message": "missing docs directory",
            }
        )
        return issues

    state_file = docs / "reconciliation-state.md"
    if require_git_state and not state_file.is_file():
        issues.append(
            {
                "severity": "error",
                "path": str(state_file),
                "message": "missing required Git reconciliation state",
            }
        )
    if state_file.is_file():
        issues.extend(audit_state_file(state_file))

    for name in REQUIRED_DIRECTORIES:
        required = docs / name
        if not required.is_dir():
            issues.append(
                {
                    "severity": "error",
                    "path": str(required),
                    "message": "missing required documentation directory",
                }
            )

    for path in sorted(docs.rglob("*")):
        if path.is_file() and path.suffix.casefold() != ".md":
            issues.append(
                {
                    "severity": "warning",
                    "path": str(path),
                    "message": "non-Markdown file under docs; persistent knowledge must be Markdown",
                }
            )

    directories = [docs, *(path for path in docs.rglob("*") if path.is_dir())]
    for directory in sorted(directories):
        index = directory / "index.md"
        if not index.is_file():
            issues.append(
                {
                    "severity": "error",
                    "path": str(index),
                    "message": "missing index.md",
                }
            )
            continue

        try:
            index_text = index.read_text(encoding="utf-8-sig")
        except (OSError, UnicodeError) as exc:
            issues.append(
                {
                    "severity": "error",
                    "path": str(index),
                    "message": f"cannot read index.md: {exc}",
                }
            )
            continue

        documents = sorted(
            path.name
            for path in directory.iterdir()
            if path.is_file() and path.suffix.casefold() == ".md" and path.name != "index.md"
        )
        subdirectories = sorted(path.name for path in directory.iterdir() if path.is_dir())

        documents_section = inventory_section(index_text, "Documents")
        subfolders_section = inventory_section(index_text, "Subfolders")
        if documents_section is None:
            issues.append(
                {
                    "severity": "error",
                    "path": str(index),
                    "message": "missing Documents inventory section",
                }
            )
            document_entries: dict[str, bool] = {}
        else:
            document_entries = inventory_entries(documents_section)

        if subfolders_section is None:
            issues.append(
                {
                    "severity": "error",
                    "path": str(index),
                    "message": "missing Subfolders inventory section",
                }
            )
            subfolder_entries: dict[str, bool] = {}
        else:
            subfolder_entries = inventory_entries(subfolders_section)

        for document in documents:
            if document not in document_entries:
                issues.append(
                    {
                        "severity": "error",
                        "path": str(index),
                        "message": f"unlisted document: {document}",
                    }
                )
            elif not document_entries[document]:
                issues.append(
                    {
                        "severity": "warning",
                        "path": str(index),
                        "message": f"document entry lacks a brief description: {document}",
                    }
                )

        for child in subdirectories:
            accepted_targets = {child, f"{child}/index.md"}
            matching_targets = accepted_targets.intersection(subfolder_entries)
            if not matching_targets:
                issues.append(
                    {
                        "severity": "error",
                        "path": str(index),
                        "message": f"unlisted subfolder: {child}",
                    }
                )
            elif not any(subfolder_entries[target] for target in matching_targets):
                issues.append(
                    {
                        "severity": "warning",
                        "path": str(index),
                        "message": f"subfolder entry lacks a brief description: {child}",
                    }
                )

        expected_documents = set(documents)
        for target in sorted(set(document_entries) - expected_documents):
            issues.append(
                {
                    "severity": "error",
                    "path": str(index),
                    "message": f"stale or non-immediate document entry: {target}",
                }
            )

        expected_subfolder_targets = {
            target
            for child in subdirectories
            for target in (child, f"{child}/index.md")
        }
        for target in sorted(set(subfolder_entries) - expected_subfolder_targets):
            issues.append(
                {
                    "severity": "error",
                    "path": str(index),
                    "message": f"stale or non-immediate subfolder entry: {target}",
                }
            )

    return issues


def main() -> int:
    args = parse_args()
    project_root = args.project_root.expanduser().resolve()
    if not project_root.is_dir():
        print(f"error: project root is not a directory: {project_root}", file=sys.stderr)
        return 2

    issues = audit(project_root, require_git_state=args.require_git_state)
    if args.as_json:
        print(json.dumps({"project_root": str(project_root), "issues": issues}, indent=2))
    elif not issues:
        print(f"Documentation audit passed: {project_root}")
    else:
        for issue in issues:
            print(f'{issue["severity"].upper():7} {issue["path"]}: {issue["message"]}')
        errors = sum(issue["severity"] == "error" for issue in issues)
        warnings = sum(issue["severity"] == "warning" for issue in issues)
        print(f"Audit found {errors} error(s) and {warnings} warning(s).")

    return 1 if any(issue["severity"] == "error" for issue in issues) else 0


if __name__ == "__main__":
    raise SystemExit(main())
