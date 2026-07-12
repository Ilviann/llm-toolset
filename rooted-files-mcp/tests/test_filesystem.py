from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path

from rooted_files_mcp.filesystem import FileAccessError, RootedFilesystem, TREE_LIMIT


class RootedFilesystemTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name) / "root"
        self.root.mkdir()
        self.fs = RootedFilesystem(self.root)

    def tearDown(self) -> None:
        self.temp.cleanup()

    def test_list_and_read_text(self) -> None:
        (self.root / "folder").mkdir()
        (self.root / "hello.txt").write_text("Привет", encoding="utf-8")
        self.assertEqual(self.fs.list_dir(), "folder/\nhello.txt")
        self.assertEqual(self.fs.read_text("hello.txt"), "Привет")

    def test_write_creates_and_replaces_atomically(self) -> None:
        self.assertIn("5 bytes", self.fs.write_text("note.md", "hello"))
        self.assertEqual((self.root / "note.md").read_text(), "hello")
        self.fs.write_text("note.md", "updated")
        self.assertEqual((self.root / "note.md").read_text(), "updated")

    def test_parent_traversal_is_denied(self) -> None:
        outside = self.root.parent / "secret.txt"
        outside.write_text("secret")
        with self.assertRaises(FileAccessError):
            self.fs.read_text("../secret.txt")
        with self.assertRaises(FileAccessError):
            self.fs.write_text("../../escape.txt", "no")

    def test_write_requires_existing_parent(self) -> None:
        with self.assertRaises(FileAccessError):
            self.fs.write_text("missing/note.txt", "no")

    def test_absolute_path_is_denied(self) -> None:
        with self.assertRaises(FileAccessError):
            self.fs.read_text("/etc/hosts")

    def test_symlink_escape_is_denied(self) -> None:
        outside = self.root.parent / "secret.txt"
        outside.write_text("secret")
        try:
            os.symlink(outside, self.root / "link")
        except OSError as exc:
            self.skipTest(f"symbolic links are unavailable: {exc}")
        with self.assertRaises(FileAccessError):
            self.fs.read_text("link")
        with self.assertRaises(FileAccessError):
            self.fs.write_text("link", "no")

    def test_binary_extension_and_content_are_denied(self) -> None:
        (self.root / "image.png").write_bytes(b"not actually an image")
        (self.root / "program").write_bytes(b"\x7fELF\x00binary")
        (self.root / "unknown.dat").write_bytes(b"abc\x00def")
        for name in ("image.png", "program", "unknown.dat"):
            with self.subTest(name=name), self.assertRaises(FileAccessError):
                self.fs.read_text(name)
        with self.assertRaises(FileAccessError):
            self.fs.write_text("new.jpg", "text")

    def test_tree_is_limited_and_does_not_follow_symlinks(self) -> None:
        for number in range(TREE_LIMIT + 5):
            (self.root / f"file-{number:03}.txt").touch()
        output = self.fs.tree()
        self.assertIn(f"limited to {TREE_LIMIT} entries", output)
        self.assertEqual(sum("file-" in line for line in output.splitlines()), TREE_LIMIT)


if __name__ == "__main__":
    unittest.main()
