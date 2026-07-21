import json
import os
import tempfile
import unittest
from pathlib import Path

from unreal_editor_mcp.discovery import read_discovery, read_token
from unreal_editor_mcp.errors import BridgeError, ErrorCode
from unreal_editor_mcp.project import ProjectLayout
from unreal_editor_mcp.platforms import PlatformAdapter


class ProjectDiscoveryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.descriptor = self.root / "Example.uproject"
        self.descriptor.write_text("{}", encoding="utf-8")
        self.layout = ProjectLayout.resolve(self.descriptor)
        self.layout.state_dir.mkdir(parents=True)
        self.platform = PlatformAdapter("macos", process_probe=lambda _pid: True)

    def tearDown(self):
        self.temporary.cleanup()

    def _write_record(self, **updates):
        value = {
            "project_hash": "a" * 40,
            "process_id": 123,
            "port": 15485,
            "bridge_version": "0.2.1",
            "unreal_version": "5.8.0-55116800",
            "updated_at_ms": 1_000_000,
        }
        value.update(updates)
        self.layout.discovery_file.write_text(json.dumps(value), encoding="utf-8")

    def test_resolves_descriptor_and_unique_project_folder(self):
        self.assertEqual(ProjectLayout.resolve(self.root), self.layout)
        (self.root / "Other.uproject").write_text("{}", encoding="utf-8")
        with self.assertRaises(Exception):
            ProjectLayout.resolve(self.root)

    def test_reads_valid_token_and_record(self):
        self.layout.token_file.write_text("b" * 64 + "\n", encoding="ascii")
        self._write_record()
        self.assertEqual(read_token(self.layout), "b" * 64)
        self.assertEqual(read_discovery(self.layout, now_ms=lambda: 1_000_010, platform=self.platform).port, 15485)

    def test_rejects_stale_or_future_record(self):
        self._write_record()
        for now in (1_020_001, 990_000):
            with self.subTest(now=now), self.assertRaises(BridgeError) as caught:
                read_discovery(self.layout, now_ms=lambda: now, platform=self.platform)
            self.assertEqual(caught.exception.code, ErrorCode.EDITOR_UNAVAILABLE)

    def test_rejects_extra_sensitive_discovery_fields(self):
        self._write_record(token="secret")
        with self.assertRaises(BridgeError) as caught:
            read_discovery(self.layout, now_ms=lambda: 1_000_000, platform=self.platform)
        self.assertEqual(caught.exception.code, ErrorCode.INVALID_CONFIGURATION)

    def test_rejects_invalid_token_and_symlink(self):
        self.layout.token_file.write_text("weak", encoding="ascii")
        with self.assertRaises(BridgeError):
            read_token(self.layout)
        self.layout.token_file.unlink()
        target = self.root / "target"
        target.write_text("a" * 64, encoding="ascii")
        try:
            os.symlink(target, self.layout.token_file)
        except (OSError, NotImplementedError):
            self.skipTest("symlinks unavailable")
        with self.assertRaises(BridgeError):
            read_token(self.layout)

    def test_rejects_oversized_discovery(self):
        self.layout.discovery_file.write_bytes(b"{" + b"x" * 5000)
        with self.assertRaises(BridgeError):
            read_discovery(self.layout)

    def test_rejects_dead_discovered_process(self):
        self._write_record()
        dead = PlatformAdapter("linux", process_probe=lambda _pid: False)
        with self.assertRaises(BridgeError) as caught:
            read_discovery(self.layout, now_ms=lambda: 1_000_000, platform=dead)
        self.assertEqual(caught.exception.code, ErrorCode.EDITOR_UNAVAILABLE)
