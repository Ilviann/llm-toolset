from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from godot_editor_mcp.assets import (
    AssetError,
    MAX_IMPORT_BYTES,
    ProjectAssets,
    _publish_no_replace,
)


class ProjectAssetsTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name)
        self.project = self.base / "game"
        self.inbox = self.base / "inbox"
        self.project.mkdir()
        self.inbox.mkdir()
        (self.project / "project.godot").write_text("[application]\n", encoding="utf-8")
        (self.project / "assets").mkdir()
        self.assets = ProjectAssets(self.project, self.inbox)

    def tearDown(self) -> None:
        self.temp.cleanup()

    def test_import_copies_allowed_file_without_overwrite(self) -> None:
        (self.inbox / "hero.png").write_bytes(b"PNG data")
        result = self.assets.import_asset("hero.png", "assets/hero.png")
        self.assertEqual(result["destination"], "res://assets/hero.png")
        self.assertEqual((self.project / "assets" / "hero.png").read_bytes(), b"PNG data")
        with self.assertRaisesRegex(AssetError, "already exists"):
            self.assets.import_asset("hero.png", "assets/hero.png")

    def test_destination_created_immediately_before_publication_is_preserved(self) -> None:
        (self.inbox / "hero.png").write_bytes(b"new bytes")
        destination = self.project / "assets" / "hero.png"

        def race(temporary, requested_destination, *, platform=None):
            self.assertEqual(Path(requested_destination).resolve(), destination.resolve())
            destination.write_bytes(b"winner bytes")
            return _publish_no_replace(temporary, requested_destination, platform=platform)

        with patch("godot_editor_mcp.assets._publish_no_replace", side_effect=race):
            with self.assertRaisesRegex(AssetError, "already exists"):
                self.assets.import_asset("hero.png", "assets/hero.png")
        self.assertEqual(destination.read_bytes(), b"winner bytes")
        self.assertEqual(list(destination.parent.glob(".godot-mcp-import-*")), [])

    def test_mocked_windows_publication_preserves_existing_destination(self) -> None:
        temporary = self.project / "assets" / ".completed.tmp"
        destination = self.project / "assets" / "hero.png"
        temporary.write_bytes(b"new bytes")
        destination.write_bytes(b"winner bytes")
        with patch("godot_editor_mcp.assets.os.rename", side_effect=FileExistsError):
            with self.assertRaises(FileExistsError):
                _publish_no_replace(temporary, destination, platform="nt")
        self.assertEqual(temporary.read_bytes(), b"new bytes")
        self.assertEqual(destination.read_bytes(), b"winner bytes")

    def test_missing_roots_raise_domain_errors(self) -> None:
        with self.assertRaisesRegex(AssetError, "project.godot"):
            ProjectAssets(self.base / "missing")
        with self.assertRaisesRegex(AssetError, "Import root"):
            ProjectAssets(self.project, self.base / "missing-inbox")

    def test_import_requires_configured_inbox_and_allowed_extension(self) -> None:
        without_inbox = ProjectAssets(self.project)
        with self.assertRaisesRegex(AssetError, "--import-root"):
            without_inbox.import_asset("hero.png", "assets/hero.png")
        (self.inbox / "program.dylib").write_bytes(b"binary")
        with self.assertRaisesRegex(AssetError, "not allowed"):
            self.assets.import_asset("program.dylib", "assets/program.dylib")

    def test_import_size_is_bounded(self) -> None:
        source = self.inbox / "huge.glb"
        with source.open("wb") as file:
            file.truncate(MAX_IMPORT_BYTES + 1)
        with self.assertRaisesRegex(AssetError, "100 MiB"):
            self.assets.import_asset("huge.glb", "assets/huge.glb")

    def test_traversal_symlink_and_protected_destinations_are_denied(self) -> None:
        outside = self.base / "outside.png"
        outside.write_bytes(b"outside")
        try:
            os.symlink(outside, self.inbox / "link.png")
        except OSError as exc:
            self.skipTest(f"symbolic links are unavailable: {exc}")
        with self.assertRaisesRegex(AssetError, "outside"):
            self.assets.import_asset("link.png", "assets/link.png")
        with self.assertRaisesRegex(AssetError, "relative"):
            self.assets.import_asset("../outside.png", "assets/outside.png")
        (self.inbox / "plugin.png").write_bytes(b"plugin")
        with self.assertRaisesRegex(AssetError, "protected"):
            self.assets.import_asset("plugin.png", "addons/plugin.png")

    def test_folder_creation_and_validation_are_confined(self) -> None:
        result = self.assets.create_folder("assets/characters/player")
        self.assertEqual(result["path"], "res://assets/characters/player")
        self.assets.validate_folder("assets/characters")
        with self.assertRaisesRegex(AssetError, "protected"):
            self.assets.create_folder(".godot/generated")

    def test_scene_paths_are_validated_without_following_escape_links(self) -> None:
        outside = self.base / "outside"
        outside.mkdir()
        try:
            os.symlink(outside, self.project / "linked")
        except OSError as exc:
            self.skipTest(f"symbolic links are unavailable: {exc}")
        with self.assertRaisesRegex(AssetError, "outside"):
            self.assets.validate_new_file("linked/test.tscn", {".tscn"})
        scene = self.project / "assets" / "existing.tscn"
        scene.write_text("[gd_scene format=3]\n", encoding="utf-8")
        self.assets.validate_file("assets/existing.tscn", {".tscn"})


if __name__ == "__main__":
    unittest.main()
