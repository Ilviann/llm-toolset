from __future__ import annotations

import tempfile
import unittest
from contextlib import redirect_stderr
from dataclasses import FrozenInstanceError
from io import StringIO
from pathlib import Path

from markdown_mcp.configuration import ConfigurationError, Settings, load_settings


class ConfigurationTests(unittest.TestCase):
    def test_settings_resolve_root_and_are_immutable(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            settings = Settings.for_root(temporary, writable=True)
            self.assertEqual(settings.root, Path(temporary).resolve())
            self.assertTrue(settings.writable)
            with self.assertRaises(FrozenInstanceError):
                settings.writable = False  # type: ignore[misc]

    def test_root_must_be_an_existing_folder(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            file_path = Path(temporary) / "file"
            file_path.write_text("x", encoding="utf-8")
            with self.assertRaisesRegex(ConfigurationError, "existing folder"):
                Settings.for_root(file_path)
            with self.assertRaisesRegex(ConfigurationError, "does not exist"):
                Settings.for_root(Path(temporary) / "missing")

    def test_cli_requires_root_and_supports_only_writable_flag(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            settings = load_settings([temporary, "--writable"])
            self.assertTrue(settings.writable)
            with redirect_stderr(StringIO()):
                with self.assertRaises(SystemExit):
                    load_settings([])
                with self.assertRaises(SystemExit):
                    load_settings([temporary, "--unknown"])


if __name__ == "__main__":
    unittest.main()
