"""Root-confined, text-only filesystem operations."""

from __future__ import annotations

import os
import stat
import tempfile
from pathlib import Path
from typing import Any, Callable

from .configuration import ConfigurationError, PROTECTED_NAMES, Settings


TREE_LIMIT = 100
MAX_TEXT_BYTES = 5 * 1024 * 1024

# Reject formats that are binary or non-source media even when their bytes happen
# to decode as UTF-8 (for example SVG).
BINARY_EXTENSIONS = {
    ".7z", ".a", ".ai", ".apk", ".app", ".arj", ".avi", ".bin", ".bmp",
    ".bz", ".bz2", ".class", ".dmg", ".doc", ".docx", ".dylib", ".ear",
    ".com", ".deb", ".eot", ".epub", ".exe", ".flac", ".gif", ".gz", ".heic", ".heif",
    ".ico", ".jar", ".jpeg", ".jpg", ".lib", ".m4a", ".m4v", ".mkv",
    ".iso", ".mov", ".mp3", ".mp4", ".mpeg", ".mpg", ".msi", ".o", ".obj", ".odp",
    ".ods", ".odt", ".ogg", ".otf", ".pdf", ".png", ".ppt", ".pptx",
    ".pyc", ".rar", ".so", ".svg", ".tar", ".tif", ".tiff", ".ttf",
    ".rpm", ".war", ".wasm", ".wav", ".webm", ".webp", ".woff", ".woff2",
    ".xls", ".xlsb", ".xlsx", ".xz", ".zip",
}

# Common executable, archive, image, audio, and document signatures.
BINARY_MAGIC = (
    b"\x7fELF", b"MZ", b"\x00asm", b"PK\x03\x04", b"PK\x05\x06",
    b"\x89PNG\r\n\x1a\n", b"\xff\xd8\xff", b"GIF87a", b"GIF89a",
    b"BM", b"%PDF-", b"\x1f\x8b", b"BZh", b"7z\xbc\xaf\x27\x1c",
    b"Rar!\x1a\x07", b"OggS", b"fLaC", b"ID3",
    b"\xca\xfe\xba\xbe", b"\xce\xfa\xed\xfe", b"\xcf\xfa\xed\xfe",
    b"\xfe\xed\xfa\xce", b"\xfe\xed\xfa\xcf",
)


class FileAccessError(Exception):
    """A safe error suitable for returning to an MCP client."""


class _HiddenPathError(FileAccessError):
    """Internal marker used to prune entries without disclosing their names."""


class HiddenPathPolicy:
    """Centralized component policy for model-facing filesystem paths."""

    ERROR = "Hidden path access is denied"

    def __init__(
        self,
        settings: Settings,
        *,
        windows: bool | None = None,
        stat_reader: Callable[[Path], Any] | None = None,
    ) -> None:
        self.root = settings.root
        self.show_hidden = settings.show_hidden
        self.allowlist = settings.hidden_allowlist
        self.case_sensitive = settings.case_sensitive
        self.windows = os.name == "nt" if windows is None else windows
        self._stat_reader = stat_reader or (lambda path: path.lstat())
        self._protected_keys = {
            self._comparison_key(item) for item in PROTECTED_NAMES
        }

    def _comparison_key(self, name: str) -> str:
        return name if self.case_sensitive else name.casefold()

    @staticmethod
    def has_windows_hidden_attribute(
        file_stat: Any, *, windows: bool = True
    ) -> bool:
        """Classify Windows stat metadata in an independently testable branch."""

        if not windows:
            return False
        attributes = getattr(file_stat, "st_file_attributes", 0)
        hidden_flag = getattr(stat, "FILE_ATTRIBUTE_HIDDEN", 0x2)
        return bool(attributes & hidden_flag)

    def _actual_entry(self, parent: Path, supplied_name: str) -> Path:
        candidate = parent / supplied_name
        if self.case_sensitive:
            return candidate
        folded_match: Path | None = None
        try:
            for entry in parent.iterdir():
                if entry.name == supplied_name:
                    return entry
                if (
                    folded_match is None
                    and entry.name.casefold() == supplied_name.casefold()
                ):
                    folded_match = entry
        except OSError:
            return candidate
        return folded_match or candidate

    def _check_component(self, name: str, entry: Path) -> None:
        if self._comparison_key(name) in self._protected_keys:
            raise _HiddenPathError(self.ERROR)

        hidden = name.startswith(".")
        if self.windows:
            try:
                hidden = hidden or self.has_windows_hidden_attribute(
                    self._stat_reader(entry), windows=True
                )
            except OSError:
                pass
        if hidden and not self.show_hidden and name not in self.allowlist:
            raise _HiddenPathError(self.ERROR)

    def _check_components(self, parts: tuple[str, ...]) -> None:
        current = self.root
        for supplied_name in parts:
            entry = self._actual_entry(current, supplied_name)
            self._check_component(entry.name, entry)
            current = entry

    def check_names(self, parts: tuple[str, ...]) -> None:
        """Apply name-only policy when a target cannot be resolved."""

        for name in parts:
            if self._comparison_key(name) in self._protected_keys:
                raise _HiddenPathError(self.ERROR)
            if (
                name.startswith(".")
                and not self.show_hidden
                and name not in self.allowlist
            ):
                raise _HiddenPathError(self.ERROR)

    def check(self, requested_parts: tuple[str, ...], resolved: Path) -> None:
        """Check both the normalized request and its resolved in-root target."""

        self._check_components(requested_parts)
        try:
            resolved_parts = resolved.relative_to(self.root).parts
        except ValueError:
            return
        self._check_components(resolved_parts)

    def allows_entry(self, entry: Path) -> bool:
        """Return whether a listing entry may be presented to the model."""

        try:
            self._check_component(entry.name, entry)
            try:
                resolved = entry.resolve(strict=False)
                resolved.relative_to(self.root)
            except (OSError, RuntimeError, ValueError):
                return True
            if resolved == entry:
                return True
            self._check_components(resolved.relative_to(self.root).parts)
            return True
        except _HiddenPathError:
            return False


class RootedFilesystem:
    def __init__(self, settings: Settings | str | os.PathLike[str]) -> None:
        if isinstance(settings, Settings):
            self.settings = settings
        else:
            try:
                self.settings = Settings.for_root(settings)
            except ConfigurationError as exc:
                raise FileAccessError(str(exc)) from None
        self.root = self.settings.root
        self.hidden_policy = HiddenPathPolicy(self.settings)

    def _require_read(self) -> None:
        if not self.settings.read:
            raise FileAccessError("Read access is disabled")

    def _require_write(self) -> None:
        if not self.settings.write:
            raise FileAccessError("Write access is disabled")

    def resolve(self, user_path: str, *, must_exist: bool = True) -> Path:
        if not isinstance(user_path, str):
            raise FileAccessError("Path must be a string")
        if "\x00" in user_path:
            raise FileAccessError("Invalid path")

        try:
            raw_path = Path(user_path)
            if raw_path.is_absolute():
                raise ValueError
            normalized = Path(os.path.normpath(user_path))
            requested_parts = () if normalized == Path(".") else normalized.parts
        except (OSError, RuntimeError, ValueError):
            raise FileAccessError("Path is outside root or does not exist") from None
        try:
            candidate = (self.root / user_path).resolve(strict=must_exist)
            candidate.relative_to(self.root)
        except (OSError, RuntimeError, ValueError):
            self.hidden_policy.check_names(requested_parts)
            raise FileAccessError("Path is outside root or does not exist") from None
        self.hidden_policy.check(requested_parts, candidate)
        return candidate

    @staticmethod
    def _entry_label(path: Path) -> str:
        if path.is_symlink():
            return f"{path.name}@"
        if path.is_dir():
            return f"{path.name}/"
        return path.name

    @staticmethod
    def _sort_key(path: Path) -> tuple[bool, str]:
        return (path.is_symlink() or not path.is_dir(), path.name.casefold())

    def list_dir(self, user_path: str = ".") -> str:
        self._require_read()
        folder = self.resolve(user_path)
        if not folder.is_dir():
            raise FileAccessError("Path is not a folder")
        try:
            entries = sorted(
                (entry for entry in folder.iterdir() if self.hidden_policy.allows_entry(entry)),
                key=self._sort_key,
            )
        except OSError as exc:
            raise FileAccessError(f"Cannot list folder: {exc.strerror or exc}") from None
        return "\n".join(self._entry_label(entry) for entry in entries) or "(empty)"

    def tree(self, user_path: str = ".") -> str:
        self._require_read()
        folder = self.resolve(user_path)
        if not folder.is_dir():
            raise FileAccessError("Path is not a folder")

        lines: list[str] = []
        count = 0
        truncated = False

        def walk(current: Path, prefix: str) -> None:
            nonlocal count, truncated
            try:
                entries = sorted(
                    (
                        entry
                        for entry in current.iterdir()
                        if self.hidden_policy.allows_entry(entry)
                    ),
                    key=self._sort_key,
                )
            except OSError:
                lines.append(f"{prefix}[unreadable]")
                return

            for index, entry in enumerate(entries):
                if count >= TREE_LIMIT:
                    truncated = True
                    return
                count += 1
                last = index == len(entries) - 1
                branch = "└── " if last else "├── "
                lines.append(prefix + branch + self._entry_label(entry))
                # Never follow directory symlinks. Reads still validate their target.
                if entry.is_dir() and not entry.is_symlink():
                    walk(entry, prefix + ("    " if last else "│   "))
                    if truncated:
                        return

        walk(folder, "")
        if truncated:
            lines.append(f"… (limited to {TREE_LIMIT} entries)")
        return "\n".join(lines) or "(empty)"

    @staticmethod
    def _reject_binary_name(path: Path) -> None:
        if path.suffix.casefold() in BINARY_EXTENSIONS:
            raise FileAccessError("Binary file access is denied")

    @staticmethod
    def _decode_text(data: bytes) -> str:
        if any(data.startswith(magic) for magic in BINARY_MAGIC) or b"\x00" in data:
            raise FileAccessError("Binary file access is denied")
        try:
            return data.decode("utf-8-sig")
        except UnicodeDecodeError:
            raise FileAccessError("File is not UTF-8 text") from None

    def _read_text_file(self, path: Path) -> str:
        if not path.is_file():
            raise FileAccessError("Path is not a file")
        self._reject_binary_name(path)
        try:
            size = path.stat().st_size
            if size > MAX_TEXT_BYTES:
                raise FileAccessError("Text file exceeds 5 MiB limit")
            return self._decode_text(path.read_bytes())
        except FileAccessError:
            raise
        except OSError as exc:
            raise FileAccessError(f"Cannot read file: {exc.strerror or exc}") from None

    def read_text(self, user_path: str) -> str:
        self._require_read()
        return self._read_text_file(self.resolve(user_path))

    def write_text(self, user_path: str, content: str) -> str:
        self._require_write()
        if not isinstance(content, str):
            raise FileAccessError("Content must be a string")
        data = content.encode("utf-8")
        if len(data) > MAX_TEXT_BYTES:
            raise FileAccessError("Text exceeds 5 MiB limit")

        path = self.resolve(user_path, must_exist=False)
        self._reject_binary_name(path)
        try:
            parent = path.parent.resolve(strict=True)
            parent.relative_to(self.root)
        except (OSError, RuntimeError, ValueError):
            raise FileAccessError("Parent folder is outside root or does not exist") from None
        if not parent.is_dir():
            raise FileAccessError("Parent is not a folder")
        if path.exists():
            if not path.is_file():
                raise FileAccessError("Path is not a file")
            # Do not allow a binary file to be replaced through the text tool.
            self._read_text_file(path)

        temp_name: str | None = None
        try:
            mode = path.stat().st_mode if path.exists() else None
            with tempfile.NamedTemporaryFile(dir=parent, prefix=".rooted-mcp-", delete=False) as temp:
                temp_name = temp.name
                temp.write(data)
                temp.flush()
                os.fsync(temp.fileno())
            if mode is not None:
                os.chmod(temp_name, stat.S_IMODE(mode))
            rechecked_path = self.resolve(user_path, must_exist=False)
            try:
                rechecked_parent = rechecked_path.parent.resolve(strict=True)
                rechecked_parent.relative_to(self.root)
            except (OSError, RuntimeError, ValueError):
                raise FileAccessError(
                    "Parent folder is outside root or does not exist"
                ) from None
            if rechecked_path != path or rechecked_parent != parent:
                raise FileAccessError("Path changed during write")
            if rechecked_path.exists():
                self._read_text_file(rechecked_path)
            os.replace(temp_name, path)
        except (FileAccessError, OSError) as exc:
            if temp_name:
                try:
                    os.unlink(temp_name)
                except OSError:
                    pass
            if isinstance(exc, FileAccessError):
                raise
            raise FileAccessError(f"Cannot write file: {exc.strerror or exc}") from None
        return f"Wrote {len(data)} bytes to {user_path}"
