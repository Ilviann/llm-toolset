#!/usr/bin/env python3
"""Audit the indexed Markdown documentation tree of one Unreal project."""

from __future__ import annotations

import argparse
import json
import re
import sys
from datetime import datetime
from pathlib import Path
from urllib.parse import unquote


REQUIRED_DIRECTORIES = (
    "gd",
    "components",
    "types",
)
LINK_PATTERN = re.compile(r"\[[^\]]+\]\(([^)]+)\)")
CACHE_UPDATED_PATTERN = re.compile(r"^- Updated: `([^`]+)`$", re.MULTILINE)
CACHE_BRANCH_PATTERN = re.compile(r"^- Branch: `([^`]+)`$", re.MULTILINE)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Check docs/index.md coverage and the required Unreal knowledge folders."
    )
    parser.add_argument("project_root", type=Path, help="Directory containing a .uproject")
    parser.add_argument("--json", action="store_true", dest="as_json")
    parser.add_argument(
        "--require-git-cache",
        action="store_true",
        help="Require and validate docs/.cache.md",
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


def audit_cache_file(cache_file: Path) -> list[dict[str, str]]:
    issues: list[dict[str, str]] = []
    try:
        cache_text = cache_file.read_text(encoding="utf-8-sig")
    except (OSError, UnicodeError) as exc:
        return [
            {
                "severity": "error",
                "path": str(cache_file),
                "message": f"cannot read documentation cache marker: {exc}",
            }
        ]

    updated_matches = CACHE_UPDATED_PATTERN.findall(cache_text)
    branch_matches = CACHE_BRANCH_PATTERN.findall(cache_text)
    if len(updated_matches) != 1:
        issues.append(
            {
                "severity": "error",
                "path": str(cache_file),
                "message": "cache marker must contain exactly one Updated field",
            }
        )
    else:
        try:
            datetime.fromisoformat(updated_matches[0])
        except ValueError:
            issues.append(
                {
                    "severity": "error",
                    "path": str(cache_file),
                    "message": "cache Updated field must be an ISO 8601 timestamp",
                }
            )
    if len(branch_matches) != 1 or not branch_matches[0].strip():
        issues.append(
            {
                "severity": "error",
                "path": str(cache_file),
                "message": "cache marker must contain exactly one non-empty Branch field",
            }
        )
    return issues


def audit(project_root: Path, require_git_cache: bool = False) -> list[dict[str, str]]:
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

    cache_file = docs / ".cache.md"
    if require_git_cache and not cache_file.is_file():
        issues.append(
            {
                "severity": "error",
                "path": str(cache_file),
                "message": "missing required Git documentation cache marker",
            }
        )
    if cache_file.is_file():
        issues.extend(audit_cache_file(cache_file))

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
            if (
                path.is_file()
                and path.suffix.casefold() == ".md"
                and path.name != "index.md"
                and not (directory == docs and path.name == ".cache.md")
            )
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

    issues = audit(project_root, require_git_cache=args.require_git_cache)
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
