from __future__ import annotations

import os
import stat
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from markdown_mcp.configuration import Settings
from markdown_mcp.filesystem import (
    MAX_MARKDOWN_BYTES,
    FileAccessError,
    MarkdownFilesystem,
)


class FilesystemTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.base = Path(self.temporary.name)
        self.root = self.base / "root"
        self.root.mkdir()
        self.readonly = MarkdownFilesystem(Settings.for_root(self.root))
        self.writable = MarkdownFilesystem(
            Settings.for_root(self.root, writable=True)
        )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write(self, name: str, data: bytes) -> Path:
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def test_whole_section_absolute_hidden_and_bom_reads(self) -> None:
        target = self.write(
            ".hidden/Guide.MD",
            b"\xef\xbb\xbf---\r\ntitle: Test\r\n---\r\n# Intro\r\narrow \xe2\x86\x92\r\n",
        )
        expected = "---\r\ntitle: Test\r\n---\r\n# Intro\r\narrow →\r\n"
        self.assertEqual(self.readonly.read_markdown(".hidden/Guide.MD"), expected)
        self.assertEqual(
            self.readonly.read_markdown(f"{target}#intro"),
            "# Intro\r\narrow →\r\n",
        )
        self.assertEqual(
            self.readonly.read_markdown(".hidden/Guide.MD#==="),
            "---\r\ntitle: Test\r\n---\r\n",
        )

    def test_listing_rejects_fragments_and_returns_shape(self) -> None:
        self.write("guide.md", b"---\n---\n# One\n### Three\n")
        self.assertEqual(
            self.readonly.list_sections("guide.md", 2),
            {
                "has_front_matter": True,
                "sections": [{"level": 1, "title": "One", "anchor": "one"}],
            },
        )
        with self.assertRaisesRegex(FileAccessError, "fragments"):
            self.readonly.list_sections("guide.md#one")

    def test_invalid_utf8_nul_extension_directory_and_missing_fail(self) -> None:
        self.write("invalid.md", b"# Good\n\xff")
        self.write("nul.md", b"# Good\n\x00")
        self.write("plain.txt", b"text")
        (self.root / "folder.md").mkdir()
        cases = (
            ("invalid.md", "UTF-8"),
            ("nul.md", "NUL"),
            ("plain.txt", "Only .md"),
            ("folder.md", "regular file"),
            ("missing.md", "does not exist"),
        )
        for path, message in cases:
            with self.subTest(path=path), self.assertRaisesRegex(
                FileAccessError, message
            ):
                self.readonly.read_markdown(path)

    def test_source_and_semantic_results_are_bounded(self) -> None:
        self.write("exact.md", b"a" * MAX_MARKDOWN_BYTES)
        self.assertEqual(
            len(self.readonly.read_markdown("exact.md")), MAX_MARKDOWN_BYTES
        )
        self.write("large.md", b"a" * (MAX_MARKDOWN_BYTES + 1))
        with self.assertRaisesRegex(FileAccessError, "256 KiB"):
            self.readonly.read_markdown("large.md")

        headings = "# same\n" * (MAX_MARKDOWN_BYTES // len("# same\n"))
        self.write("listing.md", headings.encode("utf-8"))
        with self.assertRaisesRegex(FileAccessError, "result exceeds"):
            self.readonly.list_sections("listing.md", 6)

    def test_read_only_mutations_are_rejected_before_path_access(self) -> None:
        operations = (
            lambda: self.readonly.overwrite_section("missing.md#x", "body"),
            lambda: self.readonly.append_section("missing.md", "Title", "body"),
            lambda: self.readonly.set_front_matter("missing.md", "x: y"),
            lambda: self.readonly.delete_section("missing.md#x"),
        )
        for operation in operations:
            with self.assertRaisesRegex(FileAccessError, "Write access is disabled"):
                operation()

    def test_all_mutations_preserve_bom_crlf_mode_and_surrounding_text(self) -> None:
        target = self.write(
            "guide.md",
            b"\xef\xbb\xbf===\r\nold: yes\r\n===\r\n"
            b"# Parent\r\nold\r\n## Child\r\nnested\r\n"
            b"# Sibling\r\nkeep",
        )
        if os.name != "nt":
            target.chmod(0o640)
        original_mode = stat.S_IMODE(target.stat().st_mode)

        self.writable.overwrite_section("guide.md#parent", "new")
        self.assertEqual(
            target.read_bytes(),
            b"\xef\xbb\xbf===\r\nold: yes\r\n===\r\n"
            b"# Parent\r\nnew\r\n# Sibling\r\nkeep",
        )
        self.writable.append_section("guide.md#parent", "Child", "nested")
        self.writable.set_front_matter("guide.md", "new: yes")
        self.assertEqual(
            target.read_bytes(),
            b"\xef\xbb\xbf===\r\nnew: yes\r\n===\r\n"
            b"# Parent\r\nnew\r\n## Child\r\nnested\r\n"
            b"# Sibling\r\nkeep",
        )
        self.writable.delete_section("guide.md#parent")
        self.assertEqual(
            target.read_bytes(),
            b"\xef\xbb\xbf===\r\nnew: yes\r\n===\r\n# Sibling\r\nkeep",
        )
        self.assertEqual(stat.S_IMODE(target.stat().st_mode), original_mode)

    def test_front_matter_absent_delete_is_idempotent_without_replace(self) -> None:
        self.write("guide.md", b"# Guide\n")
        with mock.patch("markdown_mcp.filesystem.os.replace") as replace:
            self.assertEqual(
                self.writable.set_front_matter("guide.md", ""),
                "Front matter updated",
            )
        replace.assert_not_called()

    def test_edited_output_limit_is_checked_before_replacement(self) -> None:
        target = self.write("guide.md", b"# Guide\n")
        with self.assertRaisesRegex(FileAccessError, "Edited Markdown exceeds"):
            self.writable.overwrite_section(
                "guide.md#guide", "x" * MAX_MARKDOWN_BYTES
            )
        self.assertEqual(target.read_bytes(), b"# Guide\n")

    def test_failed_replacement_cleans_temporary_file_and_preserves_source(self) -> None:
        target = self.write("guide.md", b"# Guide\nold\n")
        with mock.patch(
            "markdown_mcp.filesystem.os.replace", side_effect=OSError("denied")
        ):
            with self.assertRaisesRegex(FileAccessError, "Cannot replace"):
                self.writable.overwrite_section("guide.md#guide", "new")
        self.assertEqual(target.read_bytes(), b"# Guide\nold\n")
        self.assertEqual(list(self.root.glob(".guide.md.*.tmp")), [])

    def test_concurrent_source_change_fails_and_cleans_temporary_file(self) -> None:
        target = self.write("guide.md", b"# Guide\nold\n")
        original = self.writable.resolver.revalidate

        def mutate(plain_path: str, expected: Path) -> Path:
            target.write_bytes(b"# Guide\nconcurrent\n")
            return original(plain_path, expected)

        with mock.patch.object(
            self.writable.resolver, "revalidate", side_effect=mutate
        ):
            with self.assertRaisesRegex(FileAccessError, "changed during edit"):
                self.writable.overwrite_section("guide.md#guide", "new")
        self.assertEqual(target.read_bytes(), b"# Guide\nconcurrent\n")
        self.assertEqual(list(self.root.glob(".guide.md.*.tmp")), [])


if __name__ == "__main__":
    unittest.main()
