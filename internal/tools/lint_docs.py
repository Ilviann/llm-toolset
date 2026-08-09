#!/usr/bin/env python3
"""Lint the repository's indexed Markdown documentation.

This tool deliberately implements only the Markdown and YAML-front-matter
subset used by this repository. It has no third-party dependencies and does
not use documentation as an application behavior contract.
"""

from __future__ import annotations

import argparse
import ast
import os
import re
import sys
import unicodedata
from dataclasses import dataclass, field
from pathlib import Path
from urllib.parse import unquote


MAX_DOC_FILES = 10_000
MAX_DOC_DIRECTORIES = 2_000
MAX_FILE_BYTES = 2 * 1024 * 1024
MAX_TOTAL_BYTES = 64 * 1024 * 1024
MAX_LINKED_MARKDOWN_BYTES = 16 * 1024 * 1024
MAX_DIAGNOSTIC_CHARACTERS = 400
DEFAULT_MAX_DIAGNOSTICS = 200
ALLOWED_STATUSES = {"planned", "active", "completed", "deferred"}
ALLOWED_RELEASE_TRACKS = {"runtime", "support-tooling"}
IGNORED_DIRECTORY_NAMES = {
    ".git",
    ".hg",
    ".svn",
    ".venv",
    "__pycache__",
    "build",
    "dist",
    "node_modules",
    "ue-test",
    "venv",
}

ATX_HEADING_RE = re.compile(r"^(#{1,6})[ \t]+(.+?)[ \t]*#*[ \t]*$")
INLINE_LINK_RE = re.compile(
    r"!?\[[^\]\n]*\]\(\s*(?P<target><[^>\n]+>|[^\s)]+)"
)
REFERENCE_LINK_RE = re.compile(
    r"^[ \t]*\[[^\]\n]+\]:[ \t]*(?P<target><[^>\n]+>|\S+)"
)
INLINE_CODE_RE = re.compile(r"(`+)(.+?)\1")
FRONT_MATTER_KEY_RE = re.compile(r"^([a-z][a-z0-9_]*):(?:[ \t]*(.*))?$")
FRONT_MATTER_ITEM_RE = re.compile(r"^[ \t]+-[ \t]+(.+?)\s*$")
FEATURE_ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_-]*$")
BODY_DEPENDENCY_RE = re.compile(r"`([A-Za-z0-9][A-Za-z0-9_-]*)`")
BODY_DEPENDENCY_START_RE = re.compile(r"^\*\*Depends on:\*\*(.*)$", re.I)
BODY_FIELD_RE = re.compile(r"^\*\*[^*]+:\*\*")
EXPLICIT_ANCHOR_RE = re.compile(
    r"<(?:a\s+(?:name|id)|[^>]+\s+id)=[\"']([^\"']+)[\"']",
    re.I,
)


@dataclass(frozen=True, order=True)
class Diagnostic:
    path: str
    line: int
    code: str
    message: str

    def render(self) -> str:
        return f"{self.path}:{self.line}: {self.code} {self.message}"


@dataclass
class Link:
    line: int
    target: str


@dataclass
class Document:
    path: Path
    relative_path: str
    text: str
    lines: list[str]
    visible_lines: list[tuple[int, str]]
    links: list[Link] = field(default_factory=list)
    anchors: dict[str, int] = field(default_factory=dict)
    heading_names: dict[str, int] = field(default_factory=dict)


@dataclass
class Feature:
    document: Document
    feature_root: Path
    feature_id: str
    status: str
    depends_on: list[str]
    released_in: str | None
    release_track: str


class DocumentationLinter:
    def __init__(self, repository_root: Path, max_diagnostics: int = DEFAULT_MAX_DIAGNOSTICS):
        self.root = repository_root.resolve()
        self.max_diagnostics = max_diagnostics
        self.diagnostics: list[Diagnostic] = []
        self.docs_roots: list[Path] = []
        self.documents: dict[Path, Document] = {}
        self.linked_anchor_cache: dict[Path, set[str] | None] = {}
        self.total_bytes = 0
        self.linked_markdown_bytes = 0

    def lint(self) -> list[Diagnostic]:
        if not self.root.is_dir():
            self._add(self.root, 1, "DOC001", "repository root is not a directory")
            return self._sorted_diagnostics()

        self.docs_roots = self._discover_docs_roots()
        if not self.docs_roots:
            self._add(self.root, 1, "DOC021", "no documentation roots named docs were found")
            return self._sorted_diagnostics()
        for docs_root in self.docs_roots:
            self._load_docs_root(docs_root)

        for document in self.documents.values():
            self._inspect_document(document)
        for docs_root in self.docs_roots:
            self._check_indexes(docs_root)
        for document in self.documents.values():
            self._check_links(document)
        for docs_root in self.docs_roots:
            self._check_features(docs_root)

        return self._sorted_diagnostics()

    def _sorted_diagnostics(self) -> list[Diagnostic]:
        return sorted(set(self.diagnostics))

    def _add(self, path: Path, line: int, code: str, message: str) -> None:
        if len(self.diagnostics) >= self.max_diagnostics:
            return
        try:
            display = path.resolve().relative_to(self.root).as_posix()
        except (OSError, ValueError):
            display = path.as_posix()
        if len(message) > MAX_DIAGNOSTIC_CHARACTERS:
            message = message[: MAX_DIAGNOSTIC_CHARACTERS - 3] + "..."
        self.diagnostics.append(Diagnostic(display, max(1, line), code, message))

    def _discover_docs_roots(self) -> list[Path]:
        roots: list[Path] = []
        visited = 0
        for current, directories, _files in os.walk(self.root, followlinks=False):
            current_path = Path(current)
            relative = current_path.relative_to(self.root)
            if relative.parts and relative.parts[0] == "skills":
                directories[:] = []
                continue

            directories[:] = sorted(
                name
                for name in directories
                if name not in IGNORED_DIRECTORY_NAMES
                and not (current_path / name).is_symlink()
                and not (current_path == self.root and name == "skills")
            )
            visited += 1
            if visited > MAX_DOC_DIRECTORIES:
                self._add(self.root, 1, "DOC002", f"directory scan exceeds {MAX_DOC_DIRECTORIES}")
                break
            if current_path.name == "docs":
                roots.append(current_path.resolve())
                directories[:] = []
        return sorted(set(roots))

    def _load_docs_root(self, docs_root: Path) -> None:
        paths = sorted(
            path
            for path in docs_root.rglob("*.md")
            if not path.is_symlink()
            and not any(part in IGNORED_DIRECTORY_NAMES for part in path.relative_to(docs_root).parts)
        )
        if len(self.documents) + len(paths) > MAX_DOC_FILES:
            self._add(docs_root, 1, "DOC003", f"documentation file count exceeds {MAX_DOC_FILES}")
            paths = paths[: max(0, MAX_DOC_FILES - len(self.documents))]

        for path in paths:
            try:
                size = path.stat().st_size
            except OSError as error:
                self._add(path, 1, "DOC004", f"cannot stat file: {error}")
                continue
            if size > MAX_FILE_BYTES:
                self._add(path, 1, "DOC005", f"file exceeds {MAX_FILE_BYTES} bytes")
                continue
            if self.total_bytes + size > MAX_TOTAL_BYTES:
                self._add(docs_root, 1, "DOC006", f"documentation exceeds {MAX_TOTAL_BYTES} total bytes")
                break
            self.total_bytes += size
            try:
                text = path.read_text(encoding="utf-8")
            except UnicodeDecodeError as error:
                self._add(path, 1, "DOC007", f"file is not valid UTF-8: {error}")
                continue
            except OSError as error:
                self._add(path, 1, "DOC008", f"cannot read file: {error}")
                continue
            if "\x00" in text:
                self._add(path, 1, "DOC009", "file contains a NUL byte")
                continue
            resolved = path.resolve()
            self.documents[resolved] = Document(
                path=resolved,
                relative_path=resolved.relative_to(self.root).as_posix(),
                text=text,
                lines=text.splitlines(),
                visible_lines=_visible_markdown_lines(text),
            )

    def _inspect_document(self, document: Document) -> None:
        heading_count = 0
        h1_lines: list[int] = []
        anchor_counts: dict[str, int] = {}
        for line_number, line in document.visible_lines:
            heading = ATX_HEADING_RE.match(line)
            if heading:
                heading_count += 1
                level = len(heading.group(1))
                raw_name = heading.group(2).strip()
                name_key = _heading_name_key(raw_name)
                if name_key in document.heading_names:
                    first = document.heading_names[name_key]
                    self._add(
                        document.path,
                        line_number,
                        "DOC010",
                        f"duplicate section heading; first appears on line {first}",
                    )
                else:
                    document.heading_names[name_key] = line_number
                base_anchor = _github_anchor(raw_name)
                suffix = anchor_counts.get(base_anchor, 0)
                anchor_counts[base_anchor] = suffix + 1
                anchor = base_anchor if suffix == 0 else f"{base_anchor}-{suffix}"
                document.anchors[anchor] = line_number
                if level == 1:
                    h1_lines.append(line_number)

            scrubbed = INLINE_CODE_RE.sub(lambda match: " " * len(match.group(0)), line)
            for match in INLINE_LINK_RE.finditer(scrubbed):
                document.links.append(Link(line_number, _clean_link_target(match.group("target"))))
            reference = REFERENCE_LINK_RE.match(scrubbed)
            if reference:
                document.links.append(Link(line_number, _clean_link_target(reference.group("target"))))
            for explicit in EXPLICIT_ANCHOR_RE.findall(line):
                document.anchors[unquote(explicit).casefold()] = line_number

        if heading_count == 0:
            self._add(document.path, 1, "DOC011", "document has no section heading")
        elif len(h1_lines) != 1:
            line = h1_lines[1] if len(h1_lines) > 1 else 1
            self._add(document.path, line, "DOC012", "document must contain exactly one level-one heading")

    def _participating_directories(self, docs_root: Path) -> set[Path]:
        directories: set[Path] = {docs_root}
        for path in self.documents:
            try:
                path.relative_to(docs_root)
            except ValueError:
                continue
            current = path.parent
            while True:
                directories.add(current)
                if current == docs_root:
                    break
                current = current.parent
        return directories

    def _check_indexes(self, docs_root: Path) -> None:
        directories = self._participating_directories(docs_root)
        for directory in sorted(directories):
            index_path = (directory / "index.md").resolve()
            index = self.documents.get(index_path)
            if index is None:
                self._add(directory / "index.md", 1, "DOC013", "documentation directory is missing index.md")
                continue
            targets = self._resolved_local_targets(index)
            direct_pages = sorted(
                path
                for path in self.documents
                if path.parent == directory and path.name != "index.md"
            )
            for page in direct_pages:
                if page not in targets:
                    self._add(index.path, 1, "DOC014", f"index does not link immediate page {page.name}")
            child_directories = sorted(
                child for child in directories if child.parent == directory
            )
            for child in child_directories:
                child_index = (child / "index.md").resolve()
                if child not in targets and child_index not in targets:
                    self._add(index.path, 1, "DOC015", f"index does not link immediate directory {child.name}/")

    def _resolved_local_targets(self, document: Document) -> set[Path]:
        targets: set[Path] = set()
        for link in document.links:
            resolved = self._resolve_local_target(document.path, link.target)
            if resolved is not None:
                targets.add(resolved)
        return targets

    def _resolve_local_target(self, source: Path, target: str) -> Path | None:
        path_text = target.split("#", 1)[0].split("?", 1)[0]
        if not path_text or _is_external_target(path_text):
            return None
        decoded = unquote(path_text).replace("/", os.sep)
        candidate = (source.parent / decoded).resolve()
        try:
            candidate.relative_to(self.root)
        except ValueError:
            return candidate
        return candidate

    def _check_links(self, document: Document) -> None:
        for link in document.links:
            target = link.target
            path_text, separator, fragment = target.partition("#")
            path_text = path_text.split("?", 1)[0]
            if _is_external_target(path_text):
                continue
            destination = document.path if not path_text else self._resolve_local_target(document.path, path_text)
            if destination is None:
                continue
            try:
                destination.relative_to(self.root)
            except ValueError:
                self._add(document.path, link.line, "DOC016", f"local link escapes repository: {target}")
                continue
            if not destination.exists():
                self._add(document.path, link.line, "DOC017", f"local link target does not exist: {target}")
                continue
            if destination.is_dir():
                if separator and fragment:
                    destination = destination / "index.md"
                else:
                    continue
            if separator and fragment and destination.suffix.casefold() == ".md":
                target_document = self.documents.get(destination.resolve())
                anchor = unquote(fragment).casefold()
                target_anchors = (
                    set(target_document.anchors)
                    if target_document is not None
                    else self._load_linked_anchors(destination.resolve(), document, link.line)
                )
                if target_anchors is not None and anchor not in target_anchors:
                    self._add(document.path, link.line, "DOC018", f"Markdown anchor does not exist: {target}")

    def _load_linked_anchors(self, path: Path, source: Document, line: int) -> set[str] | None:
        if path in self.linked_anchor_cache:
            return self.linked_anchor_cache[path]
        try:
            size = path.stat().st_size
            if size > MAX_FILE_BYTES:
                self._add(source.path, line, "DOC019", f"linked Markdown file exceeds {MAX_FILE_BYTES} bytes: {path.name}")
                self.linked_anchor_cache[path] = None
                return None
            if self.linked_markdown_bytes + size > MAX_LINKED_MARKDOWN_BYTES:
                self._add(source.path, line, "DOC022", f"linked Markdown scan exceeds {MAX_LINKED_MARKDOWN_BYTES} bytes")
                self.linked_anchor_cache[path] = None
                return None
            self.linked_markdown_bytes += size
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError) as error:
            self._add(source.path, line, "DOC020", f"cannot inspect linked Markdown anchors: {error}")
            self.linked_anchor_cache[path] = None
            return None
        anchors = _markdown_anchors(text)
        self.linked_anchor_cache[path] = anchors
        return anchors

    def _check_features(self, docs_root: Path) -> None:
        feature_root = (docs_root / "features").resolve()
        if not feature_root.is_dir():
            return
        feature_documents = sorted(
            (
                document
                for path, document in self.documents.items()
                if _is_relative_to(path, feature_root) and path.name != "index.md"
            ),
            key=lambda document: document.relative_path,
        )
        features: dict[str, Feature] = {}
        for document in feature_documents:
            relative = document.path.relative_to(feature_root)
            if len(relative.parts) != 2:
                self._add(document.path, 1, "FEAT001", "feature document must be stored directly under features/<status>/")
                continue
            directory_status = relative.parts[0]
            values, key_lines = self._parse_front_matter(document)
            if values is None:
                continue
            required = {"feature_id", "status", "depends_on", "released_in"}
            for missing in sorted(required - values.keys()):
                self._add(document.path, 1, "FEAT002", f"front matter is missing {missing}")
            if not required.issubset(values):
                continue

            feature_id = values["feature_id"]
            status = values["status"]
            depends_on = values["depends_on"]
            released_in = values["released_in"]
            release_track = values.get("release_track", "runtime")
            if not isinstance(feature_id, str) or not FEATURE_ID_RE.fullmatch(feature_id):
                self._add(document.path, key_lines.get("feature_id", 1), "FEAT003", "feature_id must be an alphanumeric string allowing '-' and '_'")
                continue
            if feature_id != document.path.stem:
                self._add(document.path, key_lines.get("feature_id", 1), "FEAT004", f"feature_id must match filename {document.path.stem}")
            if not isinstance(status, str) or status not in ALLOWED_STATUSES:
                self._add(document.path, key_lines.get("status", 1), "FEAT005", "status must be planned, active, completed, or deferred")
                continue
            if directory_status not in ALLOWED_STATUSES or status != directory_status:
                self._add(document.path, key_lines.get("status", 1), "FEAT006", f"status must match directory {directory_status}")
            if not isinstance(depends_on, list) or any(not isinstance(item, str) or not FEATURE_ID_RE.fullmatch(item) for item in depends_on):
                self._add(document.path, key_lines.get("depends_on", 1), "FEAT007", "depends_on must be a list of feature or issue IDs")
                continue
            if len(depends_on) != len(set(depends_on)):
                self._add(document.path, key_lines.get("depends_on", 1), "FEAT008", "depends_on contains duplicate IDs")
            if feature_id in depends_on:
                self._add(document.path, key_lines.get("depends_on", 1), "FEAT009", "feature cannot depend on itself")
            if not isinstance(release_track, str) or release_track not in ALLOWED_RELEASE_TRACKS:
                self._add(
                    document.path,
                    key_lines.get("release_track", 1),
                    "FEAT023",
                    "release_track must be runtime or support-tooling",
                )
                release_track = "runtime"
            if status == "completed" and release_track == "support-tooling":
                if released_in is not None:
                    self._add(
                        document.path,
                        key_lines.get("released_in", 1),
                        "FEAT024",
                        "completed support-tooling feature must use released_in: null",
                    )
            elif status == "completed":
                if not isinstance(released_in, str) or not released_in.strip():
                    self._add(document.path, key_lines.get("released_in", 1), "FEAT010", "completed runtime feature requires a release version")
            elif released_in is not None:
                self._add(document.path, key_lines.get("released_in", 1), "FEAT011", "unreleased feature must use released_in: null")

            body_dependencies, body_line, has_section = _body_dependencies(document)
            if depends_on and not has_section:
                self._add(document.path, 1, "FEAT012", "nonempty depends_on requires a direct-prerequisite section")
            elif has_section and body_dependencies != depends_on:
                self._add(
                    document.path,
                    body_line,
                    "FEAT013",
                    f"direct-prerequisite section {body_dependencies!r} does not match front matter {depends_on!r}",
                )

            if feature_id in features:
                first = features[feature_id].document.relative_path
                self._add(document.path, key_lines.get("feature_id", 1), "FEAT014", f"duplicate feature_id; first defined in {first}")
            else:
                features[feature_id] = Feature(
                    document,
                    feature_root,
                    feature_id,
                    status,
                    depends_on,
                    released_in,
                    release_track,
                )

        issue_ids = {
            path.stem
            for path in self.documents
            if path.parent == (docs_root / "issues").resolve() and path.name != "index.md"
        }
        for feature in features.values():
            for dependency in feature.depends_on:
                target = features.get(dependency)
                if target is None:
                    if dependency not in issue_ids:
                        self._add(feature.document.path, 1, "FEAT015", f"dependency does not exist in this documentation set: {dependency}")
                    continue
                if feature.status == "completed" and target.status != "completed":
                    self._add(feature.document.path, 1, "FEAT016", f"completed feature depends on incomplete feature: {dependency}")
        self._check_dependency_cycles(features)

    def _parse_front_matter(self, document: Document) -> tuple[dict[str, object] | None, dict[str, int]]:
        if not document.lines or document.lines[0].strip() != "---":
            self._add(document.path, 1, "FEAT017", "feature document must begin with YAML front matter")
            return None, {}
        closing = next((index for index, line in enumerate(document.lines[1:65], 1) if line.strip() == "---"), None)
        if closing is None:
            self._add(document.path, 1, "FEAT018", "front matter must close within 64 lines")
            return None, {}

        values: dict[str, object] = {}
        key_lines: dict[str, int] = {}
        current_list: str | None = None
        invalid = False
        for zero_index in range(1, closing):
            line = document.lines[zero_index]
            line_number = zero_index + 1
            if not line.strip():
                continue
            item = FRONT_MATTER_ITEM_RE.match(line)
            if item and current_list:
                value = _parse_scalar(item.group(1))
                if not isinstance(value, str):
                    self._add(document.path, line_number, "FEAT019", "front-matter list items must be strings")
                    invalid = True
                else:
                    assert isinstance(values[current_list], list)
                    values[current_list].append(value)
                continue
            key_match = FRONT_MATTER_KEY_RE.match(line)
            if not key_match:
                self._add(document.path, line_number, "FEAT020", "unsupported front-matter syntax")
                invalid = True
                current_list = None
                continue
            key, raw_value = key_match.groups()
            if key in values:
                self._add(document.path, line_number, "FEAT021", f"duplicate front-matter key: {key}")
                invalid = True
                current_list = None
                continue
            key_lines[key] = line_number
            if raw_value is None or not raw_value.strip():
                values[key] = []
                current_list = key
            else:
                values[key] = _parse_scalar(raw_value.strip())
                current_list = key if isinstance(values[key], list) else None
        return (None, key_lines) if invalid else (values, key_lines)

    def _check_dependency_cycles(self, features: dict[str, Feature]) -> None:
        state: dict[str, int] = {}
        stack: list[str] = []
        reported: set[tuple[str, ...]] = set()

        def visit(feature_id: str) -> None:
            state[feature_id] = 1
            stack.append(feature_id)
            for dependency in features[feature_id].depends_on:
                if dependency not in features:
                    continue
                if state.get(dependency, 0) == 0:
                    visit(dependency)
                elif state.get(dependency) == 1:
                    start = stack.index(dependency)
                    cycle = tuple(stack[start:] + [dependency])
                    if cycle not in reported:
                        reported.add(cycle)
                        self._add(features[feature_id].document.path, 1, "FEAT022", f"feature dependency cycle: {' -> '.join(cycle)}")
            stack.pop()
            state[feature_id] = 2

        for feature_id in sorted(features):
            if state.get(feature_id, 0) == 0:
                visit(feature_id)


def _visible_markdown_lines(text: str) -> list[tuple[int, str]]:
    visible: list[tuple[int, str]] = []
    fence: str | None = None
    for line_number, line in enumerate(text.splitlines(), 1):
        stripped = line.lstrip()
        marker = stripped[:3]
        if marker in {"```", "~~~"}:
            if fence is None:
                fence = marker
            elif fence == marker:
                fence = None
            continue
        if fence is None:
            visible.append((line_number, line))
    return visible


def _heading_name_key(raw_name: str) -> str:
    name = re.sub(r"<[^>]+>", "", raw_name)
    name = re.sub(r"[`*_~]", "", name)
    return re.sub(r"\s+", " ", name).strip().casefold()


def _github_anchor(raw_name: str) -> str:
    name = re.sub(r"<[^>]+>", "", raw_name)
    name = re.sub(r"[`*_~]", "", name).strip().casefold()
    characters = [
        character
        for character in name
        if character in {"-", "_", " "}
        or character.isspace()
        or unicodedata.category(character)[0] in {"L", "N"}
    ]
    return re.sub(r"\s+", "-", "".join(characters))


def _markdown_anchors(text: str) -> set[str]:
    anchors: set[str] = set()
    counts: dict[str, int] = {}
    for _line_number, line in _visible_markdown_lines(text):
        heading = ATX_HEADING_RE.match(line)
        if heading:
            base = _github_anchor(heading.group(2).strip())
            suffix = counts.get(base, 0)
            counts[base] = suffix + 1
            anchors.add(base if suffix == 0 else f"{base}-{suffix}")
        anchors.update(unquote(anchor).casefold() for anchor in EXPLICIT_ANCHOR_RE.findall(line))
    return anchors


def _clean_link_target(target: str) -> str:
    target = target.strip()
    if target.startswith("<") and target.endswith(">"):
        return target[1:-1]
    return target


def _is_external_target(path_text: str) -> bool:
    lowered = path_text.casefold()
    if lowered.startswith(("http://", "https://", "mailto:", "data:", "app://", "//")):
        return True
    if path_text.startswith(("/", "\\")):
        return True
    return bool(re.match(r"^[A-Za-z]:[\\/]", path_text))


def _parse_scalar(raw_value: str) -> object:
    if raw_value == "null":
        return None
    if raw_value == "[]":
        return []
    if raw_value.startswith(("\"", "'")):
        try:
            value = ast.literal_eval(raw_value)
        except (SyntaxError, ValueError):
            return object()
        return value
    return raw_value


def _body_dependencies(document: Document) -> tuple[list[str], int, bool]:
    for index, line in enumerate(document.lines):
        match = BODY_DEPENDENCY_START_RE.match(line.strip())
        if not match:
            continue
        section_line = index + 1
        section = [match.group(1)]
        for following in document.lines[index + 1 :]:
            if following.startswith("#") or BODY_FIELD_RE.match(following.strip()):
                break
            section.append(following)
        dependencies = BODY_DEPENDENCY_RE.findall("\n".join(section))
        return dependencies, section_line, True
    return [], 1, False


def _is_relative_to(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
    except ValueError:
        return False
    return True


def lint_repository(repository_root: Path, max_diagnostics: int = DEFAULT_MAX_DIAGNOSTICS) -> list[Diagnostic]:
    return DocumentationLinter(repository_root, max_diagnostics=max_diagnostics).lint()


def _default_repository_root() -> Path:
    return Path(__file__).resolve().parents[2]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Lint indexed repository Markdown documentation.")
    parser.add_argument("root", nargs="?", type=Path, default=_default_repository_root(), help="repository root (defaults to this checkout)")
    parser.add_argument("--max-errors", type=int, default=DEFAULT_MAX_DIAGNOSTICS, help=f"maximum diagnostics to report (default: {DEFAULT_MAX_DIAGNOSTICS})")
    arguments = parser.parse_args(argv)
    if arguments.max_errors < 1 or arguments.max_errors > 10_000:
        parser.error("--max-errors must be between 1 and 10000")

    linter = DocumentationLinter(arguments.root, max_diagnostics=arguments.max_errors)
    diagnostics = linter.lint()
    for diagnostic in diagnostics:
        print(diagnostic.render())
    if diagnostics:
        print(f"Documentation lint failed: {len(diagnostics)} error(s).")
        return 1
    print(f"Documentation lint passed: {len(linter.documents)} file(s) across {len(linter.docs_roots)} documentation root(s).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
