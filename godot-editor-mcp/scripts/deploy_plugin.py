#!/usr/bin/env python3
"""Install and enable the Godot MCP addon, then generate MCP launch settings."""

from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import sys
import tempfile
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping


APPLICATION_ROOT = Path(__file__).resolve().parents[1]
SERVER_ENTRY = APPLICATION_ROOT / "server.py"
SOURCE_PLUGIN = APPLICATION_ROOT / "plugin" / "addons" / "godot_mcp"
PLUGIN_RESOURCE = "res://addons/godot_mcp/plugin.cfg"
SERVER_NAME = "godot-editor"
MODES = ("tiny", "small", "large")
DEFAULT_MODE = "small"
MAX_PROJECT_BYTES = 4 * 1024 * 1024
MAX_PLUGIN_FILES = 512
MAX_PLUGIN_FILE_BYTES = 2 * 1024 * 1024
MAX_PLUGIN_BYTES = 16 * 1024 * 1024
MAX_ENABLED_PLUGINS = 256
MAX_ENABLED_PLUGIN_PATH = 512
_WINDOWS_REPARSE_POINT = 0x0400
_SECTION_RE = re.compile(r"^\s*\[([^\]\r\n]+)\]\s*$")
_ENABLED_RE = re.compile(r"^(\s*)enabled\s*=\s*(.*?)\s*$")
_PACKED_STRING_ARRAY_RE = re.compile(r"PackedStringArray\s*\((.*)\)", re.DOTALL)


class DeploymentError(RuntimeError):
    """Raised when deployment input or state is unsafe or unsupported."""


@dataclass(frozen=True)
class ProjectInfo:
    folder: Path
    descriptor: Path


@dataclass(frozen=True)
class DeploymentResult:
    destination: Path
    plugin_enabled: bool
    project_changed: bool


def _resolved(path: str | Path, label: str) -> Path:
    try:
        return Path(path).expanduser().resolve(strict=True)
    except (OSError, RuntimeError, ValueError):
        raise DeploymentError(f"{label} is inaccessible") from None


def _is_link_or_reparse(path: Path) -> bool:
    try:
        return path.is_symlink() or bool(
            path.lstat().st_file_attributes & _WINDOWS_REPARSE_POINT
        )
    except AttributeError:
        return path.is_symlink()
    except OSError as error:
        raise DeploymentError(f"could not inspect path {path}: {error}") from error


def _existing_file(path: str | Path, label: str) -> Path:
    resolved = _resolved(path, label)
    if not resolved.is_file():
        raise DeploymentError(f"{label} is not a file")
    return resolved


def locate_project(folder: str | Path) -> ProjectInfo:
    root = _resolved(folder, "Godot project folder")
    if not root.is_dir():
        raise DeploymentError("Godot project path is not a folder")
    descriptor = root / "project.godot"
    if not descriptor.is_file() or _is_link_or_reparse(descriptor):
        raise DeploymentError(
            "Godot project folder must contain a regular project.godot file"
        )
    try:
        size = descriptor.stat().st_size
    except OSError as error:
        raise DeploymentError(f"could not inspect {descriptor}: {error}") from error
    if size > MAX_PROJECT_BYTES:
        raise DeploymentError("project.godot is larger than 4 MiB")
    return ProjectInfo(root, descriptor)


def _read_bounded_bytes(path: Path, maximum: int, label: str) -> bytes:
    try:
        with path.open("rb") as stream:
            data = stream.read(maximum + 1)
    except OSError as error:
        raise DeploymentError(f"could not read {label}: {error}") from error
    if len(data) > maximum:
        raise DeploymentError(f"{label} exceeds its {maximum}-byte limit")
    return data


def _project_text(project: ProjectInfo) -> tuple[bytes, str, bool]:
    original = _read_bounded_bytes(project.descriptor, MAX_PROJECT_BYTES, "project.godot")
    has_bom = original.startswith(b"\xef\xbb\xbf")
    try:
        text = original.decode("utf-8-sig")
    except UnicodeError as error:
        raise DeploymentError(f"project.godot is not valid UTF-8: {error}") from error
    if "\x00" in text:
        raise DeploymentError("project.godot contains a NUL character")
    return original, text, has_bom


def _newline_for(text: str) -> str:
    return "\r\n" if "\r\n" in text else "\n"


def _parse_enabled_plugins(value: str) -> list[str]:
    match = _PACKED_STRING_ARRAY_RE.fullmatch(value)
    if match is None:
        raise DeploymentError(
            "[editor_plugins] enabled must be one single-line PackedStringArray"
        )
    inner = match.group(1).strip()
    try:
        parsed = [] if not inner else json.loads(f"[{inner}]")
    except json.JSONDecodeError as error:
        raise DeploymentError(
            f"[editor_plugins] enabled contains unsupported string syntax: {error.msg}"
        ) from error
    if (
        not isinstance(parsed, list)
        or len(parsed) > MAX_ENABLED_PLUGINS
        or any(
            not isinstance(item, str)
            or not item
            or len(item) > MAX_ENABLED_PLUGIN_PATH
            or "\x00" in item
            for item in parsed
        )
    ):
        raise DeploymentError("[editor_plugins] enabled contains invalid plugin paths")
    return parsed


def enable_plugin_text(text: str) -> tuple[str, bool]:
    """Return project.godot text with this addon enabled, preserving other content."""
    lines = text.splitlines(keepends=True)
    sections: list[tuple[int, str]] = []
    for index, line in enumerate(lines):
        match = _SECTION_RE.fullmatch(line.rstrip("\r\n"))
        if match is not None:
            sections.append((index, match.group(1).strip()))
    editor_sections = [index for index, name in sections if name == "editor_plugins"]
    if len(editor_sections) > 1:
        raise DeploymentError("project.godot contains duplicate [editor_plugins] sections")

    newline = _newline_for(text)
    enabled_value = f"enabled=PackedStringArray({json.dumps(PLUGIN_RESOURCE)})"
    if not editor_sections:
        suffix = ""
        if text and not text.endswith(("\n", "\r")):
            suffix += newline
        if text and not (text + suffix).endswith(newline * 2):
            suffix += newline
        return text + suffix + "[editor_plugins]" + newline + enabled_value + newline, True

    section_start = editor_sections[0]
    section_end = len(lines)
    for index, _name in sections:
        if index > section_start:
            section_end = index
            break

    enabled_lines: list[tuple[int, re.Match[str]]] = []
    for index in range(section_start + 1, section_end):
        raw = lines[index].rstrip("\r\n")
        match = _ENABLED_RE.fullmatch(raw)
        if match is not None:
            enabled_lines.append((index, match))
    if len(enabled_lines) > 1:
        raise DeploymentError("[editor_plugins] contains duplicate enabled entries")
    if not enabled_lines:
        lines.insert(section_start + 1, enabled_value + newline)
        return "".join(lines), True

    index, match = enabled_lines[0]
    plugins = _parse_enabled_plugins(match.group(2))
    if PLUGIN_RESOURCE in plugins:
        return text, False
    if len(plugins) >= MAX_ENABLED_PLUGINS:
        raise DeploymentError("[editor_plugins] enabled already contains 256 plugins")
    plugins.append(PLUGIN_RESOURCE)
    ending = "\r\n" if lines[index].endswith("\r\n") else (
        "\n" if lines[index].endswith("\n") else ""
    )
    encoded = ", ".join(json.dumps(item, ensure_ascii=False) for item in plugins)
    lines[index] = f"{match.group(1)}enabled=PackedStringArray({encoded}){ending}"
    return "".join(lines), True


def _file_digest(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            while chunk := stream.read(1024 * 1024):
                digest.update(chunk)
    except OSError as error:
        raise DeploymentError(f"could not read plugin file {path}: {error}") from error
    return digest.hexdigest()


def plugin_manifest(root: Path) -> Mapping[str, tuple[int, str]]:
    if not root.is_dir() or _is_link_or_reparse(root):
        raise DeploymentError(f"plugin folder is missing or unsafe: {root}")
    manifest: dict[str, tuple[int, str]] = {}
    total = 0
    entries = 0
    try:
        paths = root.rglob("*")
        for path in paths:
            entries += 1
            if entries > MAX_PLUGIN_FILES:
                raise DeploymentError("plugin contains more than 512 filesystem entries")
            if _is_link_or_reparse(path):
                raise DeploymentError(f"plugin contains a link or reparse point: {path}")
            if path.is_dir():
                continue
            if not path.is_file():
                raise DeploymentError(f"plugin contains a non-regular file: {path}")
            size = path.stat().st_size
            if size > MAX_PLUGIN_FILE_BYTES:
                raise DeploymentError(f"plugin file is larger than 2 MiB: {path}")
            total += size
            if total > MAX_PLUGIN_BYTES:
                raise DeploymentError("plugin files exceed 16 MiB in total")
            relative = path.relative_to(root).as_posix()
            if len(relative) > 512:
                raise DeploymentError("plugin contains a path longer than 512 characters")
            manifest[relative] = (size, _file_digest(path))
    except DeploymentError:
        raise
    except OSError as error:
        raise DeploymentError(f"could not inspect plugin folder {root}: {error}") from error
    return manifest


def _metadata_version(path: Path, pattern: str, label: str) -> str:
    data = _read_bounded_bytes(path, 64 * 1024, label)
    try:
        text = data.decode("utf-8")
    except UnicodeError as error:
        raise DeploymentError(f"{label} is not valid UTF-8: {error}") from error
    match = re.search(pattern, text, re.MULTILINE)
    if match is None:
        raise DeploymentError(f"{label} does not declare a version")
    return match.group(1)


def verify_source_plugin(source: Path = SOURCE_PLUGIN) -> Mapping[str, tuple[int, str]]:
    manifest = plugin_manifest(source)
    required = {"plugin.cfg", "godot_mcp.gd"}
    if not required.issubset(manifest):
        raise DeploymentError("bundled plugin is missing plugin.cfg or godot_mcp.gd")
    plugin_version = _metadata_version(
        source / "plugin.cfg", r'^version="([^"]+)"[ \t]*\r?$', "plugin.cfg"
    )
    runtime_version = _metadata_version(
        source / "godot_mcp.gd",
        r'^const BRIDGE_VERSION := "([^"]+)"[ \t]*\r?$',
        "godot_mcp.gd",
    )
    python_version = _metadata_version(
        APPLICATION_ROOT / "godot_editor_mcp" / "__init__.py",
        r'^__version__ = "([^"]+)"[ \t]*\r?$',
        "godot_editor_mcp/__init__.py",
    )
    if len({plugin_version, runtime_version, python_version}) != 1:
        raise DeploymentError("bundled Python server and Godot plugin versions do not match")
    return manifest


def _sync_directory(folder: Path) -> None:
    if os.name == "nt":
        return
    descriptor: int | None = None
    try:
        descriptor = os.open(folder, os.O_RDONLY)
        os.fsync(descriptor)
    except OSError:
        pass
    finally:
        if descriptor is not None:
            os.close(descriptor)


def _atomic_write(path: Path, data: bytes) -> None:
    temporary: Path | None = None
    try:
        descriptor, name = tempfile.mkstemp(prefix=".project.godot.", dir=path.parent)
        temporary = Path(name)
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        try:
            os.chmod(temporary, path.stat().st_mode)
        except OSError:
            pass
        os.replace(temporary, path)
        temporary = None
        _sync_directory(path.parent)
    except OSError as error:
        raise DeploymentError(f"could not update project.godot atomically: {error}") from error
    finally:
        if temporary is not None:
            try:
                temporary.unlink()
            except FileNotFoundError:
                pass


def _safe_remove_tree(path: Path, parent: Path, prefixes: tuple[str, ...]) -> None:
    if path.parent != parent or not path.name.startswith(prefixes):
        raise DeploymentError(f"refusing to remove unexpected deployment path: {path}")
    if _is_link_or_reparse(path):
        raise DeploymentError(f"refusing to remove linked deployment path: {path}")
    if path.exists():
        shutil.rmtree(path)


def deploy(
    project: ProjectInfo,
    *,
    replace_existing: bool,
    source: Path = SOURCE_PLUGIN,
) -> DeploymentResult:
    """Transactionally install the bundled addon and enable it in project.godot."""
    source = _resolved(source, "bundled plugin folder")
    source_manifest = verify_source_plugin(source)
    original_project, text, has_bom = _project_text(project)
    updated_text, project_changed = enable_plugin_text(text)
    encoded_project = (b"\xef\xbb\xbf" if has_bom else b"") + updated_text.encode("utf-8")
    if len(encoded_project) > MAX_PROJECT_BYTES:
        raise DeploymentError("enabled project.godot would exceed 4 MiB")

    addons = project.folder / "addons"
    if addons.exists() and (not addons.is_dir() or _is_link_or_reparse(addons)):
        raise DeploymentError("project addons path is not a regular folder")
    destination = addons / "godot_mcp"
    if destination.exists() and _is_link_or_reparse(destination):
        raise DeploymentError("existing Godot MCP addon is a link or reparse point")
    if destination.exists() and not destination.is_dir():
        raise DeploymentError("existing Godot MCP addon path is not a folder")
    if destination.exists() and not replace_existing:
        raise DeploymentError("Godot MCP addon already exists; confirm replacement first")

    made_addons = not addons.exists()
    try:
        addons.mkdir(parents=False, exist_ok=True)
    except OSError as error:
        raise DeploymentError(f"could not create project addons folder: {error}") from error

    nonce = uuid.uuid4().hex
    stage = addons / f".godot_mcp.stage-{nonce}"
    backup = addons / f".godot_mcp.backup-{nonce}"
    moved_existing = False
    published = False
    project_written = False
    try:
        shutil.copytree(source, stage, symlinks=True, copy_function=shutil.copy2)
        if plugin_manifest(stage) != source_manifest:
            raise DeploymentError("staged addon does not match the bundled plugin")
        if destination.exists():
            os.replace(destination, backup)
            moved_existing = True
        os.replace(stage, destination)
        published = True
        _sync_directory(addons)
        if project_changed:
            _atomic_write(project.descriptor, encoded_project)
            project_written = True
        if plugin_manifest(destination) != source_manifest:
            raise DeploymentError("installed addon failed post-install verification")
    except Exception as error:
        rollback_errors: list[str] = []
        if project_written:
            try:
                _atomic_write(project.descriptor, original_project)
            except Exception as rollback_error:
                rollback_errors.append(f"project.godot: {rollback_error}")
        if published and destination.exists():
            try:
                _safe_remove_tree(destination, addons, ("godot_mcp",))
            except Exception as rollback_error:
                rollback_errors.append(f"new addon: {rollback_error}")
        if moved_existing and backup.exists() and not destination.exists():
            try:
                os.replace(backup, destination)
            except OSError as rollback_error:
                rollback_errors.append(f"previous addon: {rollback_error}")
        if rollback_errors:
            raise DeploymentError(
                f"deployment failed ({error}); rollback also failed: "
                + "; ".join(rollback_errors)
            ) from error
        if isinstance(error, DeploymentError):
            raise
        raise DeploymentError(f"could not install Godot MCP addon: {error}") from error
    finally:
        if stage.exists():
            _safe_remove_tree(stage, addons, (".godot_mcp.stage-",))
        if made_addons and addons.exists():
            try:
                addons.rmdir()
            except OSError:
                pass

    if backup.exists():
        _safe_remove_tree(backup, addons, (".godot_mcp.backup-",))
    if made_addons:
        _sync_directory(project.folder)
    return DeploymentResult(destination, True, project_changed)


def build_server_definition(
    project: ProjectInfo,
    mode: str = DEFAULT_MODE,
    *,
    python_executable: str | Path | None = None,
    server_path: str | Path = SERVER_ENTRY,
    godot_executable: str | Path | None = None,
) -> dict[str, object]:
    if mode not in MODES:
        raise DeploymentError("mode must be tiny, small, or large")
    python = _existing_file(
        sys.executable if python_executable is None else python_executable,
        "Python executable",
    )
    server = _existing_file(server_path, "server script")
    arguments = [str(server), str(project.folder), "--mode", mode]
    if godot_executable is not None and str(godot_executable).strip():
        if mode != "large":
            raise DeploymentError("Godot executable can be configured only in large mode")
        executable = _existing_file(godot_executable, "Godot executable")
        arguments.extend(["--godot-executable", str(executable)])
    return {"command": str(python), "args": arguments}


def format_mcp_json(server_definition: Mapping[str, object]) -> str:
    return json.dumps(
        {"mcpServers": {SERVER_NAME: dict(server_definition)}},
        ensure_ascii=False,
        indent=2,
    )


def main() -> int:
    try:
        import tkinter as tk
        from tkinter import filedialog, messagebox, scrolledtext, ttk
    except ImportError as error:
        print(f"Tkinter is required for the Godot MCP deployment helper: {error}", file=sys.stderr)
        return 1

    class DeploymentWindow:
        def __init__(self) -> None:
            self.root = tk.Tk()
            self.root.title("Godot MCP Deployment")
            self.root.minsize(820, 720)
            self.project_value = tk.StringVar()
            self.mode_value = tk.StringVar(value=DEFAULT_MODE)
            self.godot_value = tk.StringVar()
            self.status_value = tk.StringVar(
                value="Choose a Godot project. Close its editor before installing."
            )
            self.server_name_value = tk.StringVar(value=SERVER_NAME)
            self.command_value = tk.StringVar()
            self.argument_values = [tk.StringVar() for _ in range(6)]
            self._build()
            self._mode_changed()

        def _build(self) -> None:
            frame = ttk.Frame(self.root, padding=14)
            frame.grid(row=0, column=0, sticky="nsew")
            self.root.rowconfigure(0, weight=1)
            self.root.columnconfigure(0, weight=1)
            frame.columnconfigure(1, weight=1)
            frame.rowconfigure(7, weight=1)

            ttk.Label(frame, text="Godot project folder").grid(row=0, column=0, sticky="w")
            ttk.Entry(frame, textvariable=self.project_value, state="readonly").grid(
                row=0, column=1, sticky="ew", padx=8
            )
            ttk.Button(frame, text="Browse…", command=self._browse_project).grid(
                row=0, column=2
            )

            ttk.Label(frame, text="Tool mode").grid(row=1, column=0, sticky="w", pady=(12, 0))
            modes = ttk.Frame(frame)
            modes.grid(row=1, column=1, columnspan=2, sticky="w", pady=(12, 0))
            for column, mode in enumerate(MODES):
                ttk.Radiobutton(
                    modes,
                    text=mode.capitalize(),
                    value=mode,
                    variable=self.mode_value,
                    command=self._mode_changed,
                ).grid(row=0, column=column, padx=(0, 18))

            ttk.Label(frame, text="Godot executable (large mode, optional)").grid(
                row=2, column=0, sticky="w", pady=(12, 0)
            )
            self.godot_entry = ttk.Entry(frame, textvariable=self.godot_value, state="readonly")
            self.godot_entry.grid(row=2, column=1, sticky="ew", padx=8, pady=(12, 0))
            self.godot_button = ttk.Button(frame, text="Browse…", command=self._browse_godot)
            self.godot_button.grid(row=2, column=2, pady=(12, 0))

            ttk.Label(
                frame,
                text=(
                    "Close the Godot editor before installing. The helper replaces only "
                    "addons/godot_mcp and safely enables its plugin entry in project.godot."
                ),
                wraplength=780,
            ).grid(row=3, column=0, columnspan=3, sticky="w", pady=(14, 0))
            ttk.Button(
                frame, text="Install/update and enable Godot MCP", command=self._install
            ).grid(row=4, column=0, columnspan=3, sticky="ew", pady=12)
            ttk.Label(frame, textvariable=self.status_value, wraplength=780).grid(
                row=5, column=0, columnspan=3, sticky="w"
            )

            json_header = ttk.Frame(frame)
            json_header.grid(row=6, column=0, columnspan=3, sticky="ew", pady=(14, 0))
            json_header.columnconfigure(0, weight=1)
            ttk.Label(json_header, text="LM Studio mcp.json (complete object)").grid(
                row=0, column=0, sticky="w"
            )
            ttk.Button(json_header, text="Copy JSON", command=self._copy_json).grid(
                row=0, column=1, sticky="e"
            )
            self.json_text = scrolledtext.ScrolledText(
                frame, height=12, state="disabled", wrap="none"
            )
            self.json_text.grid(row=7, column=0, columnspan=3, sticky="nsew", pady=(6, 14))

            codex = ttk.LabelFrame(frame, text="ChatGPT Codex app — STDIO server", padding=10)
            codex.grid(row=8, column=0, columnspan=3, sticky="ew")
            codex.columnconfigure(1, weight=1)
            self._copyable_row(codex, 0, "Name", self.server_name_value)
            self._copyable_row(codex, 1, "Command / Python", self.command_value)
            for index, value in enumerate(self.argument_values, start=1):
                self._copyable_row(codex, index + 1, f"Argument {index}", value)

        def _copyable_row(self, parent: object, row: int, label: str, value: object) -> None:
            ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", padx=(0, 10), pady=2)
            ttk.Entry(parent, textvariable=value, state="readonly").grid(
                row=row, column=1, sticky="ew", pady=2
            )
            ttk.Button(
                parent, text="Copy", command=lambda item=value: self._copy_value(item.get())
            ).grid(row=row, column=2, padx=(10, 0), pady=2)

        def _browse_project(self) -> None:
            selected = filedialog.askdirectory(
                parent=self.root, title="Select the Godot project folder", mustexist=True
            )
            if not selected:
                return
            try:
                project = locate_project(selected)
            except DeploymentError as error:
                messagebox.showerror("Invalid Godot project", str(error), parent=self.root)
                return
            self.project_value.set(str(project.folder))
            self.status_value.set(f"Selected {project.folder.name}. Ready to install.")
            self._refresh_definition()

        def _browse_godot(self) -> None:
            selected = filedialog.askopenfilename(
                parent=self.root, title="Select the Godot executable"
            )
            if selected:
                self.godot_value.set(selected)
                self._refresh_definition()

        def _mode_changed(self) -> None:
            state = "normal" if self.mode_value.get() == "large" else "disabled"
            self.godot_button.configure(state=state)
            self.godot_entry.configure(state="readonly" if state == "normal" else "disabled")
            self._refresh_definition()

        def _definition(self) -> dict[str, object]:
            project = locate_project(self.project_value.get())
            executable = self.godot_value.get().strip() if self.mode_value.get() == "large" else None
            return build_server_definition(
                project, self.mode_value.get(), godot_executable=executable
            )

        def _refresh_definition(self) -> None:
            if not self.project_value.get():
                return
            try:
                definition = self._definition()
            except DeploymentError as error:
                self.status_value.set(str(error))
                return
            configuration = format_mcp_json(definition)
            self.json_text.configure(state="normal")
            self.json_text.delete("1.0", "end")
            self.json_text.insert("1.0", configuration)
            self.json_text.configure(state="disabled")
            self.command_value.set(str(definition["command"]))
            arguments = definition["args"]
            assert isinstance(arguments, list)
            for index, value in enumerate(self.argument_values):
                value.set(str(arguments[index]) if index < len(arguments) else "")

        def _install(self) -> None:
            try:
                project = locate_project(self.project_value.get())
                destination = project.folder / "addons" / "godot_mcp"
            except DeploymentError as error:
                messagebox.showerror("Cannot install Godot MCP", str(error), parent=self.root)
                return
            replace_existing = destination.exists()
            action = "replace the existing addon" if replace_existing else "install the addon"
            if not messagebox.askyesno(
                "Install and enable Godot MCP?",
                f"Close Godot before continuing.\n\nThis will {action} and enable it in:\n{project.descriptor}",
                parent=self.root,
            ):
                return
            try:
                result = deploy(project, replace_existing=replace_existing)
                self._refresh_definition()
            except DeploymentError as error:
                self.status_value.set("Installation failed. No partial addon should remain.")
                messagebox.showerror("Godot MCP installation failed", str(error), parent=self.root)
                return
            change = " and updated project.godot" if result.project_changed else ""
            self.status_value.set(
                f"Installed and enabled Godot MCP{change}. Configuration is ready to copy."
            )
            messagebox.showinfo(
                "Godot MCP installed",
                f"Installed at:\n{result.destination}\n\nThe addon is enabled and MCP settings are ready.",
                parent=self.root,
            )

        def _copy_json(self) -> None:
            self._copy_value(self.json_text.get("1.0", "end-1c"))

        def _copy_value(self, value: str) -> None:
            if not value:
                self.status_value.set("Choose a project before copying configuration.")
                return
            self.root.clipboard_clear()
            self.root.clipboard_append(value)
            self.root.update()
            self.status_value.set("Copied to the clipboard.")

        def run(self) -> None:
            self.root.mainloop()

    DeploymentWindow().run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
