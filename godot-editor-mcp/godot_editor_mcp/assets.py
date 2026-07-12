"""Root-confined project folders and staged asset imports."""

from __future__ import annotations

import os
import tempfile
from pathlib import Path
from typing import Any


MAX_IMPORT_BYTES = 100 * 1024 * 1024
MAX_PATH_LENGTH = 512
COPY_CHUNK_BYTES = 1024 * 1024

ALLOWED_IMPORT_EXTENSIONS = {
    ".bmp", ".csv", ".exr", ".glb", ".gltf", ".hdr", ".jpeg", ".jpg",
    ".json", ".mp3", ".obj", ".ogg", ".otf", ".png", ".svg", ".ttf",
    ".wav", ".webp",
}
PROTECTED_PROJECT_FOLDERS = {".godot", "addons"}


class AssetError(Exception):
    """A concise asset error safe to return to an MCP client."""


class ProjectAssets:
    def __init__(self, project: str | Path, import_root: str | Path | None = None) -> None:
        root = Path(project).expanduser().resolve(strict=True)
        if not root.is_dir() or not (root / "project.godot").is_file():
            raise AssetError("Project must be a folder containing project.godot")
        self.project = root
        self.import_root: Path | None = None
        if import_root is not None:
            inbox = Path(import_root).expanduser().resolve(strict=True)
            if not inbox.is_dir():
                raise AssetError("Import root must be a folder")
            self.import_root = inbox

    @staticmethod
    def _check_relative_path(user_path: Any, label: str) -> Path:
        if not isinstance(user_path, str) or not user_path or len(user_path) > MAX_PATH_LENGTH:
            raise AssetError(f"{label} must be a non-empty relative path up to 512 characters")
        segments = user_path.split("/")
        if (
            "\x00" in user_path or "\\" in user_path or user_path.startswith("res://")
            or any(part in {"", ".", ".."} for part in segments)
        ):
            raise AssetError(f"{label} must be a relative path without res://")
        relative = Path(user_path)
        if relative.is_absolute():
            raise AssetError(f"{label} must be a relative path without . or ..")
        return relative

    @staticmethod
    def _confine(root: Path, relative: Path, *, must_exist: bool) -> Path:
        try:
            candidate = (root / relative).resolve(strict=must_exist)
            candidate.relative_to(root)
        except (OSError, RuntimeError, ValueError):
            raise AssetError("Path is outside the configured root or does not exist") from None
        return candidate

    @staticmethod
    def _deny_protected(relative: Path) -> None:
        if relative.parts[0].casefold() in PROTECTED_PROJECT_FOLDERS:
            raise AssetError("Destination is a protected project folder")

    def create_folder(self, user_path: Any) -> dict[str, Any]:
        relative = self._check_relative_path(user_path, "Path")
        self._deny_protected(relative)
        folder = self._confine(self.project, relative, must_exist=False)
        if folder.exists():
            raise AssetError("Destination already exists")
        try:
            folder.mkdir(parents=True)
            folder.resolve(strict=True).relative_to(self.project)
        except (OSError, ValueError):
            raise AssetError("Cannot create project folder") from None
        return {"path": f"res://{relative.as_posix()}", "created": True}

    def validate_folder(self, user_path: Any) -> None:
        if user_path == ".":
            return
        relative = self._check_relative_path(user_path, "Folder")
        folder = self._confine(self.project, relative, must_exist=True)
        if not folder.is_dir():
            raise AssetError("Asset folder not found")

    def validate_file(self, user_path: Any, extensions: set[str] | None = None) -> None:
        relative = self._check_relative_path(user_path, "Path")
        path = self._confine(self.project, relative, must_exist=True)
        if not path.is_file():
            raise AssetError("Asset not found")
        if extensions is not None and path.suffix.casefold() not in extensions:
            raise AssetError("Asset has an unsupported extension")

    def validate_new_file(self, user_path: Any, extensions: set[str]) -> None:
        relative = self._check_relative_path(user_path, "Path")
        self._deny_protected(relative)
        if relative.suffix.casefold() not in extensions:
            raise AssetError("Destination has an unsupported extension")
        path = self._confine(self.project, relative, must_exist=False)
        if path.exists():
            raise AssetError("Destination already exists")
        try:
            parent = path.parent.resolve(strict=True)
            parent.relative_to(self.project)
        except (OSError, ValueError):
            raise AssetError("Destination folder does not exist or is outside project") from None
        if not parent.is_dir():
            raise AssetError("Destination folder does not exist")

    def import_asset(self, source_path: Any, destination_path: Any) -> dict[str, Any]:
        if self.import_root is None:
            raise AssetError("Asset import is disabled; configure --import-root")
        source_relative = self._check_relative_path(source_path, "Source")
        destination_relative = self._check_relative_path(destination_path, "Destination")
        self._deny_protected(destination_relative)

        source = self._confine(self.import_root, source_relative, must_exist=True)
        if not source.is_file():
            raise AssetError("Source is not a file")
        extension = source.suffix.casefold()
        if extension not in ALLOWED_IMPORT_EXTENSIONS:
            raise AssetError("Source file type is not allowed")
        if destination_relative.suffix.casefold() != extension:
            raise AssetError("Source and destination extensions must match")

        destination = self._confine(self.project, destination_relative, must_exist=False)
        if destination.exists():
            raise AssetError("Destination already exists")
        parent = destination.parent
        if not parent.is_dir():
            raise AssetError("Destination folder does not exist")
        try:
            parent.resolve(strict=True).relative_to(self.project)
            size = source.stat().st_size
        except (OSError, ValueError):
            raise AssetError("Cannot access source or destination") from None
        if size > MAX_IMPORT_BYTES:
            raise AssetError("Source exceeds the 100 MiB import limit")

        temp_name: str | None = None
        copied = 0
        try:
            with source.open("rb") as source_file, tempfile.NamedTemporaryFile(
                dir=parent, prefix=".godot-mcp-import-", delete=False
            ) as temp:
                temp_name = temp.name
                while chunk := source_file.read(COPY_CHUNK_BYTES):
                    copied += len(chunk)
                    if copied > MAX_IMPORT_BYTES:
                        raise AssetError("Source exceeds the 100 MiB import limit")
                    temp.write(chunk)
                temp.flush()
                os.fsync(temp.fileno())
            os.replace(temp_name, destination)
        except AssetError:
            if temp_name:
                try:
                    os.unlink(temp_name)
                except OSError:
                    pass
            raise
        except OSError:
            if temp_name:
                try:
                    os.unlink(temp_name)
                except OSError:
                    pass
            raise AssetError("Cannot copy asset into the project") from None
        return {
            "source": source_relative.as_posix(),
            "destination": f"res://{destination_relative.as_posix()}",
            "bytes": copied,
        }
