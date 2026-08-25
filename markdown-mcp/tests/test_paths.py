from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from markdown_mcp.paths import (
    MarkdownPathResolver,
    PathAccessError,
    is_markdown_path,
    split_markdown_fragment,
)


class PathTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.base = Path(self.temporary.name)
        self.root = self.base / "root"
        self.root.mkdir()
        (self.root / "guide.md").write_text("# Guide\n", encoding="utf-8")
        self.resolver = MarkdownPathResolver(self.root)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_extensions_and_last_supported_fragment(self) -> None:
        self.assertTrue(is_markdown_path("FILE.MARKDOWN"))
        self.assertFalse(is_markdown_path("file.txt"))
        self.assertEqual(
            split_markdown_fragment("name#draft.md"),
            ("name#draft.md", None),
        )
        self.assertEqual(
            split_markdown_fragment("name#draft.md#part"),
            ("name#draft.md", "part"),
        )
        self.assertEqual(
            split_markdown_fragment("name.md#one#two"),
            ("name.md#one#two", None),
        )

    def test_relative_absolute_hidden_and_literal_hash_paths(self) -> None:
        hidden = self.root / ".hidden" / "#notes.MD"
        hidden.parent.mkdir()
        hidden.write_text("# Hidden\n", encoding="utf-8")
        relative = self.resolver.resolve("guide.md#guide")
        absolute = self.resolver.resolve(f"{self.root / 'guide.md'}#guide")
        self.assertEqual(relative.path, absolute.path)
        self.assertEqual(relative.fragment, "guide")
        literal = self.resolver.resolve(".hidden/#notes.MD")
        self.assertEqual(literal.path, hidden.resolve())
        self.assertIsNone(literal.fragment)

    def test_fragment_can_be_forbidden(self) -> None:
        with self.assertRaisesRegex(PathAccessError, "fragments"):
            self.resolver.resolve("guide.md#guide", allow_fragment=False)

    def test_traversal_sibling_prefix_missing_directory_and_extension_fail(self) -> None:
        outside = self.base / "outside.md"
        outside.write_text("# Outside\n", encoding="utf-8")
        sibling = self.base / "root-other"
        sibling.mkdir()
        (sibling / "other.md").write_text("# Other\n", encoding="utf-8")
        (self.root / "folder.md").mkdir()

        for path in (
            "../outside.md",
            str(sibling / "other.md"),
            "missing.md",
        ):
            with self.subTest(path=path), self.assertRaisesRegex(
                PathAccessError, "outside root or does not exist"
            ):
                self.resolver.resolve(path)
        with self.assertRaisesRegex(PathAccessError, "regular file"):
            self.resolver.resolve("folder.md")
        with self.assertRaisesRegex(PathAccessError, "Only .md"):
            self.resolver.resolve("guide.txt")

    def test_invalid_path_values_fail_stably(self) -> None:
        for value in ("", "bad\x00.md", "x" * 4097):
            with self.subTest(value=value[:20]), self.assertRaisesRegex(
                PathAccessError, "Invalid path"
            ):
                self.resolver.resolve(value)
        with self.assertRaisesRegex(PathAccessError, "string"):
            self.resolver.resolve(3)  # type: ignore[arg-type]

    def test_symlink_escape_and_resolved_extension_are_rejected(self) -> None:
        outside = self.base / "outside.md"
        outside.write_text("# Outside\n", encoding="utf-8")
        try:
            os.symlink(outside, self.root / "escape.md")
        except OSError:
            self.skipTest("symlinks are unavailable")
        with self.assertRaisesRegex(PathAccessError, "outside root"):
            self.resolver.resolve("escape.md")

        plain = self.root / "plain.txt"
        plain.write_text("plain", encoding="utf-8")
        os.symlink(plain, self.root / "alias.md")
        with self.assertRaisesRegex(PathAccessError, "Only .md"):
            self.resolver.resolve("alias.md")

    @unittest.skipUnless(os.name == "nt", "Windows junction policy")
    def test_windows_junction_escape_is_rejected(self) -> None:
        outside = self.base / "outside"
        outside.mkdir()
        (outside / "escape.md").write_text("# Outside\n", encoding="utf-8")
        junction = self.root / "junction"
        result = subprocess.run(
            ["cmd", "/c", "mklink", "/J", str(junction), str(outside)],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode != 0:
            self.skipTest("junction creation is unavailable")
        try:
            with self.assertRaisesRegex(PathAccessError, "outside root"):
                self.resolver.resolve("junction/escape.md")
        finally:
            os.rmdir(junction)


if __name__ == "__main__":
    unittest.main()
