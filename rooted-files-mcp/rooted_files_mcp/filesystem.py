"""Root-confined, text-only filesystem operations."""

from __future__ import annotations

import os
import stat
import tempfile
from pathlib import Path


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


class RootedFilesystem:
    def __init__(self, root: str | os.PathLike[str]) -> None:
        candidate = Path(root).expanduser().resolve(strict=True)
        if not candidate.is_dir():
            raise FileAccessError("Root is not a folder")
        self.root = candidate

    def resolve(self, user_path: str, *, must_exist: bool = True) -> Path:
        if not isinstance(user_path, str):
            raise FileAccessError("Path must be a string")
        if "\x00" in user_path:
            raise FileAccessError("Invalid path")

        try:
            candidate = (self.root / user_path).resolve(strict=must_exist)
            candidate.relative_to(self.root)
        except (OSError, RuntimeError, ValueError):
            raise FileAccessError("Path is outside root or does not exist") from None
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
        folder = self.resolve(user_path)
        if not folder.is_dir():
            raise FileAccessError("Path is not a folder")
        try:
            entries = sorted(folder.iterdir(), key=self._sort_key)
        except OSError as exc:
            raise FileAccessError(f"Cannot list folder: {exc.strerror or exc}") from None
        return "\n".join(self._entry_label(entry) for entry in entries) or "(empty)"

    def tree(self, user_path: str = ".") -> str:
        folder = self.resolve(user_path)
        if not folder.is_dir():
            raise FileAccessError("Path is not a folder")

        lines: list[str] = []
        count = 0
        truncated = False

        def walk(current: Path, prefix: str) -> None:
            nonlocal count, truncated
            try:
                entries = sorted(current.iterdir(), key=self._sort_key)
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

    def read_text(self, user_path: str) -> str:
        path = self.resolve(user_path)
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

    def write_text(self, user_path: str, content: str) -> str:
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
            self.read_text(user_path)

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
            os.replace(temp_name, path)
        except OSError as exc:
            if temp_name:
                try:
                    os.unlink(temp_name)
                except OSError:
                    pass
            raise FileAccessError(f"Cannot write file: {exc.strerror or exc}") from None
        return f"Wrote {len(data)} bytes to {user_path}"
