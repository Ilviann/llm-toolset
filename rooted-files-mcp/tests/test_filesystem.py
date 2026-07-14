from __future__ import annotations

import os
import re
import stat
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from rooted_files_mcp.filesystem import (
    MAX_TEXT_BYTES,
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
        operations = (
            lambda fs: fs.write_text("note.txt", "new"),
            lambda fs: fs.write_lines("note.txt", 1, 1, "new"),
        )
        for operation in operations:
            with self.subTest(operation=operation):
                target.write_text("old", encoding="utf-8")
                fs = self.hidden_fs()
                fs.hidden_policy.windows = True
                with mock.patch.object(
                    fs.hidden_policy,
                    "has_windows_hidden_attribute",
                    side_effect=[False, False, True],
                ):
                    with self.assertRaisesRegex(
                        FileAccessError, "Hidden path access is denied"
                    ):
                        operation(fs)
                self.assertEqual(target.read_text(encoding="utf-8"), "old")
                self.assertEqual(list(self.root.glob(".rooted-mcp-*")), [])

    def test_read_text_selects_first_middle_last_blank_and_all_lines(self) -> None:
        target = self.root / "lines.txt"
        target.write_bytes(b"first\n\nthird\nfourth")
        self.assertEqual(self.fs.read_text("lines.txt", 1, 1), "first\n")
        self.assertEqual(self.fs.read_text("lines.txt", 2, 3), "\nthird\n")
        self.assertEqual(self.fs.read_text("lines.txt", 4, 4), "fourth")
        self.assertEqual(
            self.fs.read_text("lines.txt", 1, 4), "first\n\nthird\nfourth"
        )

    def test_write_lines_replaces_expands_contracts_and_deletes_ranges(self) -> None:
        target = self.root / "edit.txt"
        cases = (
            (1, 1, "FIRST", b"FIRST\ntwo\nthree\nfour\n"),
            (2, 3, "middle", b"one\nmiddle\nfour\n"),
            (4, 4, "LAST", b"one\ntwo\nthree\nLAST\n"),
            (2, 2, "two-a\ntwo-b", b"one\ntwo-a\ntwo-b\nthree\nfour\n"),
            (2, 3, "short", b"one\nshort\nfour\n"),
            (2, 3, "", b"one\nfour\n"),
            (1, 4, "replacement", b"replacement\n"),
        )
        for start, end, content, expected in cases:
            with self.subTest(start=start, end=end, content=content):
                target.write_bytes(b"one\ntwo\nthree\nfour\n")
                summary = self.fs.write_lines(
                    "edit.txt", start, end, content
                )
                self.assertEqual(target.read_bytes(), expected)
                self.assertIn(f"lines {start}-{end}", summary)
                self.assertIn(f"{len(expected)} UTF-8 bytes", summary)

    def test_line_tools_preserve_endings_bom_and_multibyte_text(self) -> None:
        target = self.root / "formats.txt"
        target.write_bytes("\ufeffα\r\nβ\r\nγ".encode("utf-8"))
        self.assertEqual(self.fs.read_text("formats.txt", 1, 2), "α\r\nβ\r\n")
        self.fs.write_lines("formats.txt", 2, 2, "новый\nряд")
        self.assertEqual(
            target.read_bytes(),
            "\ufeffα\r\nновый\r\nряд\r\nγ".encode("utf-8"),
        )
        self.fs.write_lines("formats.txt", 4, 4, "конец\n")
        self.assertEqual(
            target.read_bytes(),
            "\ufeffα\r\nновый\r\nряд\r\nконец".encode("utf-8"),
        )
        self.assertFalse(target.read_bytes().endswith(b"\n"))

    def test_mixed_endings_use_the_selected_or_nearby_convention(self) -> None:
        target = self.root / "mixed.txt"
        target.write_bytes(b"one\nsecond\r\nthird")
        self.fs.write_lines("mixed.txt", 2, 2, "a\nb")
        self.assertEqual(target.read_bytes(), b"one\na\r\nb\r\nthird")
        self.fs.write_lines("mixed.txt", 4, 4, "last-a\nlast-b")
        self.assertEqual(
            target.read_bytes(), b"one\na\r\nb\r\nlast-a\r\nlast-b"
        )

    def test_line_number_validation_has_stable_errors(self) -> None:
        (self.root / "two.txt").write_text("one\ntwo", encoding="utf-8")
        for start, end in ((1, None), (None, 1)):
            with self.subTest(start=start, end=end), self.assertRaisesRegex(
                FileAccessError,
                "Start line and end line must be provided together",
            ):
                self.fs.read_text("two.txt", start, end)
        cases = (
            (0, 1, "Line numbers must be one-based"),
            (-1, 1, "Line numbers must be one-based"),
            (2, 1, "Start line must not exceed end line"),
            (1.0, 1, "Line numbers must be integers"),
            (True, 1, "Line numbers must be integers"),
            (1, False, "Line numbers must be integers"),
            (1, 3, "Line range exceeds file line count (2)"),
        )
        for start, end, message in cases:
            with self.subTest(start=start, end=end):
                with self.assertRaisesRegex(FileAccessError, f"^{re.escape(message)}\\Z"):
                    self.fs.read_text("two.txt", start, end)
                with self.assertRaisesRegex(FileAccessError, f"^{re.escape(message)}\\Z"):
                    self.fs.write_lines("two.txt", start, end, "x")

    def test_compiler_and_git_hunk_coordinates_map_directly(self) -> None:
        target = self.root / "coordinates.txt"
        target.write_text("1\n2\n3\n4\n5\n", encoding="utf-8")
        compiler_line = 3
        self.assertEqual(
            self.fs.read_text("coordinates.txt", compiler_line, compiler_line),
            "3\n",
        )
        new_start, new_count = 2, 3
        self.assertEqual(
            self.fs.read_text(
                "coordinates.txt", new_start, new_start + new_count - 1
            ),
            "2\n3\n4\n",
        )
        omitted_count = 1
        self.assertEqual(
            self.fs.read_text(
                "coordinates.txt", 5, 5 + omitted_count - 1
            ),
            "5\n",
        )
        with self.assertRaisesRegex(FileAccessError, "one-based"):
            self.fs.read_text("coordinates.txt", 0, 0)

    def test_empty_and_bom_only_files_have_no_addressable_lines(self) -> None:
        for name, data in (("empty.txt", b""), ("bom.txt", b"\xef\xbb\xbf")):
            (self.root / name).write_bytes(data)
            with self.subTest(name=name), self.assertRaisesRegex(
                FileAccessError, "File contains no addressable lines"
            ):
                self.fs.read_text(name, 1, 1)
            with self.subTest(name=name), self.assertRaisesRegex(
                FileAccessError, "File contains no addressable lines"
            ):
                self.fs.write_lines(name, 1, 1, "new")

    def test_line_tools_reject_every_invalid_text_classification(self) -> None:
        invalid = {
            "missing.txt": None,
            "folder": "folder",
            "known.png": b"plain text",
            "signature": b"\x7fELFpayload",
            "nul.txt": b"one\ntwo\x00bad",
            "utf8.txt": b"one\ntwo\xffbad",
            "large.txt": b"x" * (MAX_TEXT_BYTES + 1),
        }
        for name, data in invalid.items():
            path = self.root / name
            if data == "folder":
                path.mkdir()
            elif data is not None:
                path.write_bytes(data)
            with self.subTest(name=name):
                with self.assertRaises(FileAccessError):
                    self.fs.read_text(name, 1, 1)
                with self.assertRaises(FileAccessError):
                    self.fs.write_lines(name, 1, 1, "x")

        valid = self.root / "result.txt"
        valid.write_text("x", encoding="utf-8")
        with self.assertRaisesRegex(FileAccessError, "Text exceeds 5 MiB limit"):
            self.fs.write_lines("result.txt", 1, 1, "x" * (MAX_TEXT_BYTES + 1))
        self.assertEqual(valid.read_text(encoding="utf-8"), "x")

    def test_line_tools_enforce_permissions_features_paths_and_symlinks(self) -> None:
        (self.root / "visible.txt").write_text("one\ntwo\n", encoding="utf-8")
        (self.root / ".secret").write_text("secret\n", encoding="utf-8")
        outside = self.root.parent / "outside-lines.txt"
        outside.write_text("outside\n", encoding="utf-8")
        try:
            os.symlink(outside, self.root / "outside-link")
            os.symlink(".secret", self.root / "hidden-link")
        except OSError as exc:
            self.skipTest(f"symbolic links are unavailable: {exc}")

        hidden = self.hidden_fs()
        for operation in (
            lambda: hidden.read_text(".secret", 1, 1),
            lambda: hidden.write_lines(".secret", 1, 1, "no"),
            lambda: hidden.read_text("hidden-link", 1, 1),
            lambda: hidden.write_lines("hidden-link", 1, 1, "no"),
        ):
            with self.assertRaisesRegex(FileAccessError, "Hidden path access is denied"):
                operation()
        for operation in (
            lambda: self.fs.read_text("../outside-lines.txt", 1, 1),
            lambda: self.fs.write_lines("../outside-lines.txt", 1, 1, "no"),
            lambda: self.fs.read_text("outside-link", 1, 1),
            lambda: self.fs.write_lines("outside-link", 1, 1, "no"),
        ):
            with self.assertRaises(FileAccessError):
                operation()

        no_read = RootedFilesystem(replace(self.fs.settings, read=False))
        no_write = RootedFilesystem(replace(self.fs.settings, write=False))
        with self.assertRaisesRegex(FileAccessError, "Read access is disabled"):
            no_read.read_text("visible.txt", 1, 1)
        with self.assertRaisesRegex(FileAccessError, "Write access is disabled"):
            no_write.write_lines("visible.txt", 1, 1, "no")

    def test_write_lines_failure_preserves_original_mode_and_cleans_temp(self) -> None:
        target = self.root / "atomic.txt"
        target.write_text("old\ntext\n", encoding="utf-8")
        target.chmod(0o640)
        original_mode = stat.S_IMODE(target.stat().st_mode)
        with mock.patch("rooted_files_mcp.filesystem.os.replace", side_effect=OSError("boom")):
            with self.assertRaisesRegex(FileAccessError, "Cannot write file"):
                self.fs.write_lines("atomic.txt", 1, 1, "new")
        self.assertEqual(target.read_text(encoding="utf-8"), "old\ntext\n")
        self.assertEqual(stat.S_IMODE(target.stat().st_mode), original_mode)
        self.assertEqual(list(self.root.glob(".rooted-mcp-*")), [])


if __name__ == "__main__":
    unittest.main()
