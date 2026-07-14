from __future__ import annotations

import os
import tempfile
import unittest
from dataclasses import FrozenInstanceError
from pathlib import Path

from rooted_files_mcp.configuration import (
    BUILTIN_HIDDEN_ALLOWLIST,
    MAX_CONFIG_BYTES,
    MAX_HIDDEN_ALLOWLIST_ENTRIES,
    MAX_HIDDEN_NAME_LENGTH,
    ConfigurationError,
    load_settings,
)


class ConfigurationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name)
        self.workspace = self.base / "workspace with spaces"
        self.workspace.mkdir()

    def tearDown(self) -> None:
        self.temp.cleanup()

    def write_config(self, content: str | bytes) -> Path:
        config_dir = self.workspace / ".mcp"
        config_dir.mkdir(exist_ok=True)
        path = config_dir / "rooted-files-mcp.ini"
        if isinstance(content, bytes):
            path.write_bytes(content)
        else:
            path.write_text(content, encoding="utf-8")
        return path

    def test_missing_configuration_preserves_legacy_defaults(self) -> None:
        settings = load_settings(root=self.workspace)
        self.assertEqual(settings.workspace, self.workspace.resolve())
        self.assertEqual(settings.root, self.workspace.resolve())
        self.assertTrue(settings.read)
        self.assertTrue(settings.write)
        self.assertTrue(settings.show_hidden)

    def test_configuration_only_startup_uses_native_relative_root(self) -> None:
        exposed = self.workspace / "source files"
        exposed.mkdir()
        self.write_config(
            "[paths]\n"
            f"root = {exposed.relative_to(self.workspace)}\n"
            "[permissions]\nread = false\nwrite = true\n"
            "[features]\nshow_hidden = false\n"
        )
        settings = load_settings(workspace=str(self.workspace))
        self.assertEqual(settings.root, exposed.resolve())
        self.assertFalse(settings.read)
        self.assertTrue(settings.write)
        self.assertFalse(settings.show_hidden)

    def test_current_directory_is_the_configuration_only_workspace(self) -> None:
        exposed = self.workspace / "root"
        exposed.mkdir()
        self.write_config("[paths]\nroot = root\n")
        settings = load_settings(cwd=self.workspace)
        self.assertEqual(settings.workspace, self.workspace.resolve())
        self.assertEqual(settings.root, exposed.resolve())

    def test_cli_values_override_ini_including_explicit_false(self) -> None:
        configured = self.workspace / "configured"
        configured.mkdir()
        explicit = self.base / "explicit root"
        explicit.mkdir()
        self.write_config(
            "[paths]\nroot = configured\n"
            "[permissions]\nread = true\nwrite = false\n"
            "[features]\nshow_hidden = true\n"
        )
        settings = load_settings(
            root=explicit,
            workspace=self.workspace,
            read=False,
            write=True,
            show_hidden=False,
        )
        self.assertEqual(settings.root, explicit.resolve())
        self.assertFalse(settings.read)
        self.assertTrue(settings.write)
        self.assertFalse(settings.show_hidden)

    def test_settings_are_frozen(self) -> None:
        settings = load_settings(root=self.workspace)
        with self.assertRaises(FrozenInstanceError):
            settings.read = False  # type: ignore[misc]
        with self.assertRaises(AttributeError):
            settings.hidden_allowlist.add(".other")  # type: ignore[attr-defined]

    def test_hidden_allowlist_is_additive_and_trimmed(self) -> None:
        self.write_config(
            "[paths]\nroot = .\n"
            "[features]\nshow_hidden = false\nhidden_allowlist =\n"
            "    .editorconfig\n"
            "    .github  \n"
        )
        settings = load_settings(workspace=self.workspace)
        self.assertEqual(
            settings.hidden_allowlist,
            BUILTIN_HIDDEN_ALLOWLIST | {".editorconfig", ".github"},
        )

    def test_invalid_hidden_allowlist_names_are_rejected(self) -> None:
        cases = {
            "empty": "",
            "dot": ".",
            "dot dot": "..",
            "forward separator": ".hidden/name",
            "back separator": ".hidden\\name",
            "duplicate": ".extra\n    .extra",
            "built-in duplicate": ".gitignore",
            "protected": ".mcp",
            "protected case alias": ".MCP",
            "too long": "x" * (MAX_HIDDEN_NAME_LENGTH + 1),
            "too many": "\n    ".join(
                f".hidden-{index}" for index in range(MAX_HIDDEN_ALLOWLIST_ENTRIES + 1)
            ),
        }
        for label, value in cases.items():
            with self.subTest(label=label):
                self.write_config(
                    "[paths]\nroot = .\n[features]\nhidden_allowlist = "
                    f"{value}\n"
                )
                with self.assertRaisesRegex(ConfigurationError, "Hidden allowlist"):
                    load_settings(workspace=self.workspace)

    def test_configuration_only_startup_requires_root(self) -> None:
        self.write_config("[permissions]\nread = true\n")
        with self.assertRaisesRegex(ConfigurationError, r"\[paths\] root"):
            load_settings(workspace=self.workspace)

    def test_missing_configuration_and_root_are_rejected(self) -> None:
        with self.assertRaisesRegex(ConfigurationError, "configuration file is missing"):
            load_settings(workspace=self.workspace)

    def test_malformed_duplicate_and_invalid_boolean_are_rejected(self) -> None:
        cases = {
            "malformed": "root = .\n",
            "duplicate": "[paths]\nroot = .\nroot = .\n",
            "invalid boolean": "[paths]\nroot = .\n[permissions]\nread = perhaps\n",
        }
        for label, content in cases.items():
            with self.subTest(label=label):
                self.write_config(content)
                with self.assertRaises(ConfigurationError):
                    load_settings(workspace=self.workspace)

    def test_unknown_sections_keys_and_defaults_are_rejected(self) -> None:
        cases = {
            "section": "[paths]\nroot = .\n[permission]\nread = false\n",
            "key": "[paths]\nroot = .\n[permissions]\nraed = false\n",
            "removed line access": (
                "[paths]\nroot = .\n[features]\nline_access = false\n"
            ),
            "default": "[DEFAULT]\nread = false\n[paths]\nroot = .\n",
        }
        for label, content in cases.items():
            with self.subTest(label=label):
                self.write_config(content)
                with self.assertRaisesRegex(ConfigurationError, "Unknown"):
                    load_settings(workspace=self.workspace)

    def test_oversized_nul_and_invalid_utf8_files_are_rejected(self) -> None:
        cases = {
            "oversized": b"#" * (MAX_CONFIG_BYTES + 1),
            "nul": b"[paths]\nroot = .\x00\n",
            "utf8": b"[paths]\nroot = \xff\n",
        }
        for label, content in cases.items():
            with self.subTest(label=label):
                self.write_config(content)
                with self.assertRaises(ConfigurationError):
                    load_settings(workspace=self.workspace)

    def test_configured_root_cannot_escape_by_traversal_or_symlink(self) -> None:
        outside = self.base / "outside"
        outside.mkdir()
        cases = ["../outside"]
        link = self.workspace / "linked-root"
        try:
            os.symlink(outside, link, target_is_directory=True)
        except OSError:
            pass
        else:
            cases.append("linked-root")

        for configured_root in cases:
            with self.subTest(root=configured_root):
                self.write_config(f"[paths]\nroot = {configured_root}\n")
                with self.assertRaisesRegex(ConfigurationError, "inside workspace"):
                    load_settings(workspace=self.workspace)

    def test_configuration_file_cannot_escape_through_symlink(self) -> None:
        outside = self.base / "outside.ini"
        outside.write_text("[paths]\nroot = .\n", encoding="utf-8")
        config_dir = self.workspace / ".mcp"
        config_dir.mkdir()
        try:
            os.symlink(outside, config_dir / "rooted-files-mcp.ini")
        except OSError as exc:
            self.skipTest(f"symbolic links are unavailable: {exc}")
        with self.assertRaisesRegex(ConfigurationError, "inside workspace"):
            load_settings(root=self.workspace)

    def test_inaccessible_configured_root_is_validated_even_with_cli_root(self) -> None:
        self.write_config("[paths]\nroot = missing\n")
        with self.assertRaisesRegex(ConfigurationError, "inaccessible"):
            load_settings(root=self.workspace, workspace=self.workspace)


if __name__ == "__main__":
    unittest.main()
