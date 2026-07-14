"""Root-confined, text-only filesystem operations."""

from __future__ import annotations

import os
import stat
import tempfile
from dataclasses import dataclass
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


@dataclass(frozen=True)
class _LineScan:
    """Validated line metadata with optional source bytes for replacement."""

    selected_text: str
    line_count: int
    has_bom: bool
    ended_with_newline: bool
    nearby_newline: bytes
    raw_lines: tuple[bytes, ...] | None = None


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
    def _reject_binary_bytes(data: bytes, *, prefix: bool = True) -> None:
        has_signature = prefix and any(
            data.startswith(magic) for magic in BINARY_MAGIC
        )
        if has_signature or b"\x00" in data:
            raise FileAccessError("Binary file access is denied")

    @staticmethod
    def _decode_utf8(data: bytes, *, strip_bom: bool = True) -> str:
        try:
            return data.decode("utf-8-sig" if strip_bom else "utf-8")
        except UnicodeDecodeError:
            raise FileAccessError("File is not UTF-8 text") from None

    def _validate_text_path(self, path: Path) -> None:
        if not path.is_file():
            raise FileAccessError("Path is not a file")
        self._reject_binary_name(path)
        try:
            size = path.stat().st_size
        except OSError as exc:
            raise FileAccessError(f"Cannot read file: {exc.strerror or exc}") from None
        if size > MAX_TEXT_BYTES:
            raise FileAccessError("Text file exceeds 5 MiB limit")

    def _read_text_bytes(self, path: Path) -> bytes:
        self._validate_text_path(path)
        try:
            with path.open("rb") as source:
                data = source.read(MAX_TEXT_BYTES + 1)
        except OSError as exc:
            raise FileAccessError(f"Cannot read file: {exc.strerror or exc}") from None
        if len(data) > MAX_TEXT_BYTES:
            raise FileAccessError("Text file exceeds 5 MiB limit")
        self._reject_binary_bytes(data)
        return data

    def _read_text_file(self, path: Path) -> str:
        return self._decode_utf8(self._read_text_bytes(path))

    @staticmethod
    def _validate_line_range(start_line: Any, end_line: Any) -> tuple[int, int]:
        if (
            isinstance(start_line, bool)
            or isinstance(end_line, bool)
            or not isinstance(start_line, int)
            or not isinstance(end_line, int)
        ):
            raise FileAccessError("Line numbers must be integers")
        if start_line < 1 or end_line < 1:
            raise FileAccessError("Line numbers must be one-based")
        if start_line > end_line:
            raise FileAccessError("Start line must not exceed end line")
        return start_line, end_line

    @staticmethod
    def _line_ending(raw_line: bytes) -> bytes | None:
        if raw_line.endswith(b"\r\n"):
            return b"\r\n"
        if raw_line.endswith(b"\n"):
            return b"\n"
        return None

    def _scan_text_lines(
        self,
        path: Path,
        start_line: int,
        end_line: int,
        *,
        retain_source: bool = False,
    ) -> _LineScan:
        """Validate the full file while retaining only requested read text."""

        self._validate_text_path(path)
        selected: list[str] = []
        raw_lines: list[bytes] | None = [] if retain_source else None
        endings: list[bytes | None] = []
        line_count = 0
        byte_count = 0
        has_bom = False
        prefix_length = max(len(magic) for magic in BINARY_MAGIC)

        try:
            with path.open("rb") as source:
                prefix = source.read(prefix_length)
                self._reject_binary_bytes(prefix)
                source.seek(0)
                for raw_line in source:
                    byte_count += len(raw_line)
                    if byte_count > MAX_TEXT_BYTES:
                        raise FileAccessError("Text file exceeds 5 MiB limit")
                    self._reject_binary_bytes(raw_line, prefix=False)
                    if line_count == 0 and raw_line.startswith(b"\xef\xbb\xbf"):
                        has_bom = True
                        raw_line = raw_line[3:]
                        if not raw_line:
                            continue
                    decoded = self._decode_utf8(raw_line, strip_bom=False)
                    line_count += 1
                    endings.append(self._line_ending(raw_line))
                    if raw_lines is not None:
                        raw_lines.append(raw_line)
                    if start_line <= line_count <= end_line:
                        selected.append(decoded)
        except FileAccessError:
            raise
        except OSError as exc:
            raise FileAccessError(f"Cannot read file: {exc.strerror or exc}") from None

        if end_line > line_count:
            if line_count == 0:
                raise FileAccessError("File contains no addressable lines")
            raise FileAccessError(f"Line range exceeds file line count ({line_count})")

        nearby: bytes | None = next(
            (
                ending
                for ending in endings[start_line - 1 : end_line]
                if ending is not None
            ),
            None,
        )
        if nearby is None:
            nearby = next(
                (ending for ending in reversed(endings[: start_line - 1]) if ending),
                None,
            )
        if nearby is None:
            nearby = next((ending for ending in endings[end_line:] if ending), b"\n")

        return _LineScan(
            selected_text="".join(selected),
            line_count=line_count,
            has_bom=has_bom,
            ended_with_newline=bool(endings and endings[-1] is not None),
            nearby_newline=nearby,
            raw_lines=tuple(raw_lines) if raw_lines is not None else None,
        )

    def read_text(
        self,
        user_path: str,
        start_line: Any = None,
        end_line: Any = None,
    ) -> str:
        self._require_read()
        if (start_line is None) != (end_line is None):
            raise FileAccessError(
                "Start line and end line must be provided together"
            )
        path = self.resolve(user_path)
        if start_line is None:
            return self._read_text_file(path)
        start_line, end_line = self._validate_line_range(start_line, end_line)
        return self._scan_text_lines(path, start_line, end_line).selected_text

    def _write_target(self, user_path: str, *, must_exist: bool) -> tuple[Path, Path]:
        path = self.resolve(user_path, must_exist=must_exist)
        self._reject_binary_name(path)
        try:
            parent = path.parent.resolve(strict=True)
            parent.relative_to(self.root)
        except (OSError, RuntimeError, ValueError):
            raise FileAccessError("Parent folder is outside root or does not exist") from None
        if not parent.is_dir():
            raise FileAccessError("Parent is not a folder")
        if path.exists():
            self._read_text_file(path)
        elif must_exist:
            raise FileAccessError("Path is outside root or does not exist")
        return path, parent

    def _replace_atomically(
        self,
        user_path: str,
        path: Path,
        parent: Path,
        data: bytes,
        *,
        must_exist: bool,
    ) -> None:
        temp_name: str | None = None
        try:
            mode = path.stat().st_mode if path.exists() else None
            with tempfile.NamedTemporaryFile(
                dir=parent, prefix=".rooted-mcp-", delete=False
            ) as temp:
                temp_name = temp.name
                temp.write(data)
                temp.flush()
                os.fsync(temp.fileno())
            if mode is not None:
                os.chmod(temp_name, stat.S_IMODE(mode))

            rechecked_path = self.resolve(user_path, must_exist=must_exist)
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
            elif must_exist:
                raise FileAccessError("Path changed during write")
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

    def write_text(self, user_path: str, content: str) -> str:
        self._require_write()
        if not isinstance(content, str):
            raise FileAccessError("Content must be a string")
        try:
            data = content.encode("utf-8")
        except UnicodeEncodeError:
            raise FileAccessError("Content is not valid UTF-8 text") from None
        if len(data) > MAX_TEXT_BYTES:
            raise FileAccessError("Text exceeds 5 MiB limit")

        path, parent = self._write_target(user_path, must_exist=False)
        self._replace_atomically(user_path, path, parent, data, must_exist=False)
        return f"Wrote {len(data)} bytes to {user_path}"

    @staticmethod
    def _replacement_bytes(
        content: str,
        newline: bytes,
        *,
        needs_terminator: bool,
    ) -> bytes:
        """Normalize replacement separators and apply the required boundary."""

        if not content:
            return b""
        normalized = content.replace("\r\n", "\n")
        if normalized.endswith("\n"):
            normalized = normalized[:-1]
        if not needs_terminator:
            normalized = normalized.rstrip("\n")
        normalized = normalized.replace("\n", newline.decode("ascii"))
        if needs_terminator:
            normalized += newline.decode("ascii")
        try:
            return normalized.encode("utf-8")
        except UnicodeEncodeError:
            raise FileAccessError("Content is not valid UTF-8 text") from None

    def write_lines(
        self,
        user_path: str,
        start_line: Any,
        end_line: Any,
        content: Any,
    ) -> str:
        self._require_write()
        start_line, end_line = self._validate_line_range(start_line, end_line)
        if not isinstance(content, str):
            raise FileAccessError("Content must be a string")

        path, parent = self._write_target(user_path, must_exist=True)
        scan = self._scan_text_lines(
            path, start_line, end_line, retain_source=True
        )
        assert scan.raw_lines is not None
        through_eof = end_line == scan.line_count
        replacement = self._replacement_bytes(
            content,
            scan.nearby_newline,
            needs_terminator=(not through_eof or scan.ended_with_newline),
        )
        prefix = b"\xef\xbb\xbf" if scan.has_bom else b""
        data = b"".join(
            (
                prefix,
                *scan.raw_lines[: start_line - 1],
                replacement,
                *scan.raw_lines[end_line:],
            )
        )
        if len(data) > MAX_TEXT_BYTES:
            raise FileAccessError("Text exceeds 5 MiB limit")

        self._replace_atomically(
            user_path, path, parent, data, must_exist=True
        )
        return (
            f"Replaced lines {start_line}-{end_line}; "
            f"result is {len(data)} UTF-8 bytes"
        )
