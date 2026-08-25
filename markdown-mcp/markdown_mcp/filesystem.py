"""Bounded UTF-8 Markdown I/O and same-directory atomic editing."""

from __future__ import annotations

import json
import os
import stat
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from .configuration import Settings
from .markdown import (
    MarkdownError,
    append_section as append_section_text,
    decode_fragment,
    delete_section as delete_section_text,
    overwrite_section as overwrite_section_text,
    section_listing,
    select_markdown,
    set_front_matter as set_front_matter_text,
)
from .paths import MarkdownPathResolver, PathAccessError, ResolvedMarkdownPath


MAX_MARKDOWN_BYTES = 256 * 1024
UTF8_BOM = b"\xef\xbb\xbf"


class FileAccessError(Exception):
    """A stable filesystem or resource-limit error."""


@dataclass(frozen=True)
class _Snapshot:
    resolved: ResolvedMarkdownPath
    raw: bytes
    text: str
    has_bom: bool
    identity: tuple[int, int]
    mode: int


def _identity(metadata: os.stat_result) -> tuple[int, int]:
    return metadata.st_dev, metadata.st_ino


class MarkdownFilesystem:
    """Own all filesystem access beneath one immutable root."""

    def __init__(self, settings: Settings | str | os.PathLike[str]) -> None:
        if isinstance(settings, Settings):
            self.settings = settings
        else:
            self.settings = Settings.for_root(settings)
        self.root = self.settings.root
        self.resolver = MarkdownPathResolver(self.root)

    def _require_writable(self) -> None:
        if not self.settings.writable:
            raise FileAccessError("Write access is disabled")

    @staticmethod
    def _read_path(path: Path) -> tuple[bytes, os.stat_result]:
        try:
            before = path.stat()
            if not stat.S_ISREG(before.st_mode):
                raise FileAccessError("Path is not a regular file")
            if before.st_size > MAX_MARKDOWN_BYTES:
                raise FileAccessError("Markdown file exceeds 256 KiB limit")
            with path.open("rb") as source:
                opened = os.fstat(source.fileno())
                if not stat.S_ISREG(opened.st_mode) or _identity(opened) != _identity(before):
                    raise FileAccessError("File changed during access")
                data = source.read(MAX_MARKDOWN_BYTES + 1)
                after = os.fstat(source.fileno())
                if _identity(after) != _identity(opened):
                    raise FileAccessError("File changed during access")
        except FileAccessError:
            raise
        except OSError:
            raise FileAccessError("Cannot read Markdown file") from None
        if len(data) > MAX_MARKDOWN_BYTES:
            raise FileAccessError("Markdown file exceeds 256 KiB limit")
        if b"\x00" in data:
            raise FileAccessError("Markdown file contains NUL bytes")
        return data, opened

    @staticmethod
    def _decode(data: bytes) -> tuple[str, bool]:
        has_bom = data.startswith(UTF8_BOM)
        content = data[len(UTF8_BOM) :] if has_bom else data
        try:
            return content.decode("utf-8"), has_bom
        except UnicodeDecodeError:
            raise FileAccessError("Markdown file is not valid UTF-8") from None

    def _load(
        self, model_path: str, *, allow_fragment: bool = True
    ) -> _Snapshot:
        try:
            resolved = self.resolver.resolve(
                model_path, allow_fragment=allow_fragment
            )
        except PathAccessError as exc:
            raise FileAccessError(str(exc)) from None
        raw, metadata = self._read_path(resolved.path)
        text, has_bom = self._decode(raw)
        return _Snapshot(
            resolved,
            raw,
            text,
            has_bom,
            _identity(metadata),
            stat.S_IMODE(metadata.st_mode),
        )

    @staticmethod
    def _validate_input(value: str, label: str) -> None:
        if not isinstance(value, str):
            raise FileAccessError(f"{label} must be a string")
        try:
            size = len(value.encode("utf-8"))
        except UnicodeEncodeError:
            raise FileAccessError(f"{label} is not valid Unicode") from None
        if size > MAX_MARKDOWN_BYTES:
            raise FileAccessError(f"{label} exceeds 256 KiB limit")

    @staticmethod
    def _ensure_result(value: str) -> str:
        try:
            size = len(value.encode("utf-8"))
        except UnicodeEncodeError:
            raise FileAccessError("Tool result is not valid Unicode") from None
        if size > MAX_MARKDOWN_BYTES:
            raise FileAccessError("Tool result exceeds 256 KiB limit")
        return value

    @classmethod
    def _ensure_structured_result(cls, value: dict[str, object]) -> None:
        encoded = json.dumps(
            value, ensure_ascii=False, separators=(",", ":")
        )
        cls._ensure_result(encoded)

    @staticmethod
    def _decoded_fragment(snapshot: _Snapshot, *, required: bool) -> str | None:
        raw = snapshot.resolved.fragment
        if raw is None:
            if required:
                raise FileAccessError("A Markdown heading fragment is required")
            return None
        try:
            return decode_fragment(raw)
        except MarkdownError as exc:
            raise FileAccessError(str(exc)) from None

    def read_markdown(self, model_path: str) -> str:
        snapshot = self._load(model_path)
        fragment = self._decoded_fragment(snapshot, required=False)
        try:
            output = (
                snapshot.text
                if fragment is None
                else select_markdown(snapshot.text, fragment)
            )
        except MarkdownError as exc:
            raise FileAccessError(str(exc)) from None
        return self._ensure_result(output)

    def list_sections(
        self, model_path: str, max_level: int = 3
    ) -> dict[str, object]:
        snapshot = self._load(model_path, allow_fragment=False)
        try:
            output = section_listing(snapshot.text, max_level)
        except MarkdownError as exc:
            raise FileAccessError(str(exc)) from None
        self._ensure_structured_result(output)
        return output

    def _encode_edit(self, snapshot: _Snapshot, text: str) -> bytes:
        try:
            body = text.encode("utf-8")
        except UnicodeEncodeError:
            raise FileAccessError("Edited Markdown is not valid Unicode") from None
        data = (UTF8_BOM if snapshot.has_bom else b"") + body
        if len(data) > MAX_MARKDOWN_BYTES:
            raise FileAccessError("Edited Markdown exceeds 256 KiB limit")
        if b"\x00" in data:
            raise FileAccessError("Edited Markdown contains NUL bytes")
        return data

    def _atomic_replace(self, snapshot: _Snapshot, data: bytes) -> None:
        path = snapshot.resolved.path
        parent = path.parent
        descriptor = -1
        temporary: str | None = None
        try:
            descriptor, temporary = tempfile.mkstemp(
                prefix=f".{path.name}.", suffix=".tmp", dir=parent
            )
            with os.fdopen(descriptor, "wb") as target:
                descriptor = -1
                target.write(data)
                target.flush()
                os.fsync(target.fileno())
            os.chmod(temporary, snapshot.mode)

            try:
                current_path = self.resolver.revalidate(
                    snapshot.resolved.plain_path, path
                )
            except PathAccessError as exc:
                raise FileAccessError(str(exc)) from None
            current, metadata = self._read_path(current_path)
            if (
                _identity(metadata) != snapshot.identity
                or stat.S_IMODE(metadata.st_mode) != snapshot.mode
                or current != snapshot.raw
            ):
                raise FileAccessError("File changed during edit")
            os.replace(temporary, path)
            temporary = None
            self._fsync_parent(parent)
        except FileAccessError:
            raise
        except OSError:
            raise FileAccessError("Cannot replace Markdown file") from None
        finally:
            if descriptor >= 0:
                os.close(descriptor)
            if temporary is not None:
                try:
                    os.unlink(temporary)
                except OSError:
                    pass

    @staticmethod
    def _fsync_parent(parent: Path) -> None:
        flags = getattr(os, "O_RDONLY", 0)
        if hasattr(os, "O_DIRECTORY"):
            flags |= os.O_DIRECTORY
        descriptor = -1
        try:
            descriptor = os.open(parent, flags)
            os.fsync(descriptor)
        except OSError:
            pass
        finally:
            if descriptor >= 0:
                os.close(descriptor)

    def _edit(
        self,
        snapshot: _Snapshot,
        transform: Callable[[str], str],
    ) -> None:
        self._require_writable()
        try:
            edited = transform(snapshot.text)
        except MarkdownError as exc:
            raise FileAccessError(str(exc)) from None
        data = self._encode_edit(snapshot, edited)
        if data != snapshot.raw:
            self._atomic_replace(snapshot, data)

    def overwrite_section(self, model_path: str, body: str) -> str:
        self._require_writable()
        self._validate_input(body, "body")
        snapshot = self._load(model_path)
        fragment = self._decoded_fragment(snapshot, required=True)
        assert fragment is not None
        self._edit(
            snapshot,
            lambda text: overwrite_section_text(text, fragment, body),
        )
        return "Section overwritten"

    def append_section(
        self, model_path: str, title: str, body: str
    ) -> str:
        self._require_writable()
        self._validate_input(title, "title")
        self._validate_input(body, "body")
        snapshot = self._load(model_path)
        fragment = self._decoded_fragment(snapshot, required=False)
        self._edit(
            snapshot,
            lambda text: append_section_text(text, fragment, title, body),
        )
        return "Section appended"

    def set_front_matter(self, model_path: str, body: str) -> str:
        self._require_writable()
        self._validate_input(body, "body")
        snapshot = self._load(model_path, allow_fragment=False)
        self._edit(snapshot, lambda text: set_front_matter_text(text, body))
        return "Front matter updated"

    def delete_section(self, model_path: str) -> str:
        self._require_writable()
        snapshot = self._load(model_path)
        fragment = self._decoded_fragment(snapshot, required=True)
        assert fragment is not None
        self._edit(snapshot, lambda text: delete_section_text(text, fragment))
        return "Section deleted"
