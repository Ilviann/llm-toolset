from __future__ import annotations

import os
import stat
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from rooted_files_mcp.filesystem import (
    TREE_LIMIT,
    FileAccessError,
    HiddenPathPolicy,
    RootedFilesystem,
)


class RootedFilesystemTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name) / "root"
        self.root.mkdir()
        self.fs = RootedFilesystem(self.root)

    def tearDown(self) -> None:
        self.temp.cleanup()

    def hidden_fs(self, *allowed: str, show_hidden: bool = False) -> RootedFilesystem:
        allowlist = self.fs.settings.hidden_allowlist | frozenset(allowed)
        settings = replace(
            self.fs.settings,
            show_hidden=show_hidden,
            hidden_allowlist=allowlist,
        )
        return RootedFilesystem(settings)

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

    def test_hidden_policy_builtins_exact_names_and_ordinary_dots(self) -> None:
        for name in (
            ".gitignore",
            ".env.template",
            ".git",
            ".env",
            ".env.template.local",
            "ordinary.name.txt",
        ):
            (self.root / name).write_text(name, encoding="utf-8")
        fs = self.hidden_fs()
        self.assertEqual(
            fs.list_dir(),
            ".env.template\n.gitignore\nordinary.name.txt",
        )
        self.assertEqual(fs.read_text(".gitignore"), ".gitignore")
        for denied in (".git", ".env", ".env.template.local", ".missing"):
            with self.subTest(path=denied), self.assertRaisesRegex(
                FileAccessError, "^Hidden path access is denied$"
            ):
                fs.read_text(denied)

    def test_configured_allowlist_applies_to_files_and_nested_folders(self) -> None:
        (self.root / ".editorconfig").write_text("config", encoding="utf-8")
        allowed_folder = self.root / ".github" / ".github"
        allowed_folder.mkdir(parents=True)
        (allowed_folder / "workflow.yml").write_text("ok", encoding="utf-8")
        (allowed_folder / ".secret").write_text("no", encoding="utf-8")
        fs = self.hidden_fs(".editorconfig", ".github")

        self.assertIn(".github/", fs.list_dir())
        self.assertEqual(
            fs.read_text(".github/.github/workflow.yml"),
            "ok",
        )
        self.assertNotIn(".secret", fs.tree(".github"))
        with self.assertRaisesRegex(FileAccessError, "Hidden path access is denied"):
            fs.read_text(".github/.github/.secret")

    def test_hidden_write_targets_are_denied_and_allowlisted_target_is_allowed(self) -> None:
        fs = self.hidden_fs(".notes")
        with self.assertRaisesRegex(FileAccessError, "^Hidden path access is denied$"):
            fs.write_text(".private", "no")
        self.assertFalse((self.root / ".private").exists())
        fs.write_text(".notes", "yes")
        self.assertEqual((self.root / ".notes").read_text(encoding="utf-8"), "yes")

    def test_protected_mcp_name_is_always_omitted_and_denied(self) -> None:
        protected = self.root / ".mcp"
        protected.mkdir()
        config = protected / "rooted-files-mcp.ini"
        config.write_text("secret", encoding="utf-8")
        nested = self.root / "visible" / ".mcp"
        nested.mkdir(parents=True)
        (nested / "secret.txt").write_text("secret", encoding="utf-8")
        protected_file_parent = self.root / "protected-file-case"
        protected_file_parent.mkdir()
        (protected_file_parent / ".mcp").write_text("secret", encoding="utf-8")

        for show_hidden in (False, True):
            fs = self.hidden_fs(show_hidden=show_hidden)
            with self.subTest(show_hidden=show_hidden):
                self.assertNotIn(".mcp", fs.list_dir())
                self.assertNotIn(".mcp", fs.tree())
                self.assertEqual(fs.list_dir("protected-file-case"), "(empty)")
                for path in (
                    ".mcp",
                    ".mcp/rooted-files-mcp.ini",
                    "visible/.mcp/secret.txt",
                    "protected-file-case/.mcp",
                ):
                    with self.assertRaisesRegex(
                        FileAccessError, "^Hidden path access is denied$"
                    ):
                        fs.resolve(path)
                with self.assertRaisesRegex(FileAccessError, "Hidden path access is denied"):
                    fs.write_text("visible/.mcp/new.txt", "no")

    def test_tree_prunes_hidden_entries_before_limit_accounting(self) -> None:
        for number in range(TREE_LIMIT + 5):
            (self.root / f".hidden-{number:03}").touch()
        for number in range(3):
            (self.root / f"visible-{number}.txt").touch()
        fs = self.hidden_fs()
        output = fs.tree()
        self.assertEqual(sum("visible-" in line for line in output.splitlines()), 3)
        self.assertNotIn("hidden", output)
        self.assertNotIn("limited", output)

    def test_hidden_only_listing_and_tree_are_empty(self) -> None:
        (self.root / ".private").touch()
        fs = self.hidden_fs()
        self.assertEqual(fs.list_dir(), "(empty)")
        self.assertEqual(fs.tree(), "(empty)")

    def test_symlink_aliases_enforce_requested_and_resolved_hidden_paths(self) -> None:
        (self.root / ".secret").write_text("hidden", encoding="utf-8")
        (self.root / ".gitignore").write_text("allowed", encoding="utf-8")
        (self.root / "visible.txt").write_text("visible", encoding="utf-8")
        (self.root / ".mcp").mkdir()
        (self.root / ".mcp" / "secret.txt").write_text("protected", encoding="utf-8")
        links = {
            "visible-to-denied": ".secret",
            "visible-to-allowed": ".gitignore",
            ".env": "visible.txt",
            ".env.template": "visible.txt",
            "visible-to-protected": ".mcp/secret.txt",
        }
        try:
            for name, target in links.items():
                os.symlink(target, self.root / name)
        except OSError as exc:
            self.skipTest(f"symbolic links are unavailable: {exc}")

        fs = self.hidden_fs()
        self.assertEqual(fs.read_text("visible-to-allowed"), "allowed")
        self.assertEqual(fs.read_text(".env.template"), "visible")
        listing = fs.list_dir()
        self.assertIn("visible-to-allowed@", listing)
        self.assertIn(".env.template@", listing)
        for denied in ("visible-to-denied", ".env", "visible-to-protected"):
            with self.subTest(path=denied), self.assertRaisesRegex(
                FileAccessError, "^Hidden path access is denied$"
            ):
                fs.read_text(denied)
            self.assertNotIn(f"{denied}@", listing)

    def test_out_of_root_symlink_remains_denied_with_both_visibility_modes(self) -> None:
        outside = self.root.parent / ".outside-secret"
        outside.write_text("secret", encoding="utf-8")
        try:
            os.symlink(outside, self.root / "visible-link")
        except OSError as exc:
            self.skipTest(f"symbolic links are unavailable: {exc}")
        for show_hidden in (False, True):
            with self.subTest(show_hidden=show_hidden), self.assertRaisesRegex(
                FileAccessError, "outside root"
            ):
                self.hidden_fs(show_hidden=show_hidden).read_text("visible-link")

    def test_case_matching_uses_actual_entry_name(self) -> None:
        (self.root / ".gitignore").write_text("allowed", encoding="utf-8")
        insensitive = replace(
            self.fs.settings,
            show_hidden=False,
            case_sensitive=False,
        )
        policy = HiddenPathPolicy(insensitive)
        policy.check((".GITIGNORE",), self.root / ".gitignore")

        (self.root / ".MCP").mkdir()
        with self.assertRaisesRegex(FileAccessError, "Hidden path access is denied"):
            policy.check((".MCP",), self.root / ".MCP")

        sensitive = HiddenPathPolicy(
            replace(insensitive, case_sensitive=True, show_hidden=True)
        )
        sensitive.check((".MCP",), self.root / ".MCP")

    def test_windows_hidden_attribute_classification_is_isolated(self) -> None:
        hidden = SimpleNamespace(st_file_attributes=stat.FILE_ATTRIBUTE_HIDDEN)
        visible = SimpleNamespace(st_file_attributes=0)
        self.assertTrue(HiddenPathPolicy.has_windows_hidden_attribute(hidden))
        self.assertFalse(HiddenPathPolicy.has_windows_hidden_attribute(visible))
        self.assertFalse(
            HiddenPathPolicy.has_windows_hidden_attribute(hidden, windows=False)
        )

    def test_windows_attribute_policy_covers_files_folders_and_reparse_entries(self) -> None:
        hidden_flag = stat.FILE_ATTRIBUTE_HIDDEN
        hidden_names = {"hidden.txt", "hidden-folder", "hidden-link", "desktop.ini"}
        observed: list[str] = []

        def fake_stat(path: Path) -> SimpleNamespace:
            observed.append(path.name)
            attributes = hidden_flag if path.name in hidden_names else 0
            return SimpleNamespace(st_file_attributes=attributes)

        settings = replace(
            self.fs.settings,
            show_hidden=False,
            hidden_allowlist=self.fs.settings.hidden_allowlist | {"desktop.ini"},
        )
        policy = HiddenPathPolicy(settings, windows=True, stat_reader=fake_stat)
        policy.check(("desktop.ini",), self.root / "desktop.ini")
        for parts in (
            ("hidden.txt",),
            ("hidden-folder", "child.txt"),
            ("hidden-link",),
            (".dot-name",),
        ):
            with self.subTest(parts=parts), self.assertRaisesRegex(
                FileAccessError, "Hidden path access is denied"
            ):
                policy.check(parts, self.root.joinpath(*parts))
        self.assertIn("hidden-folder", observed)

        posix_reader = mock.Mock(side_effect=AssertionError("unexpected stat read"))
        posix_policy = HiddenPathPolicy(
            settings, windows=False, stat_reader=posix_reader
        )
        posix_policy.check(("visible.txt",), self.root / "visible.txt")
        posix_reader.assert_not_called()

    def test_windows_attribute_change_before_replace_aborts_and_cleans_temp(self) -> None:
        target = self.root / "note.txt"
        target.write_text("old", encoding="utf-8")
        fs = self.hidden_fs()
        fs.hidden_policy.windows = True
        with mock.patch.object(
            fs.hidden_policy,
            "has_windows_hidden_attribute",
            side_effect=[False, False, True],
        ):
            with self.assertRaisesRegex(FileAccessError, "Hidden path access is denied"):
                fs.write_text("note.txt", "new")
        self.assertEqual(target.read_text(encoding="utf-8"), "old")
        self.assertEqual(list(self.root.glob(".rooted-mcp-*")), [])


if __name__ == "__main__":
    unittest.main()
