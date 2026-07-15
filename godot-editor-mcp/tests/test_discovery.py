from __future__ import annotations

import json
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

from godot_editor_mcp.discovery import (
    DISCOVERY_FILE,
    discovered_port,
    normalized_project_path,
    project_path_hash,
    read_discovery_record,
)
from godot_editor_mcp.errors import ProjectMismatchError


class DiscoveryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.project = Path(self.temporary.name).resolve()
        (self.project / "project.godot").write_text("[application]\n", encoding="utf-8")
        (self.project / ".godot").mkdir()
        self.path = self.project / ".godot" / DISCOVERY_FILE

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write_record(self, **changes: object) -> None:
        record: dict[str, object] = {
            "process_id": 123,
            "project_hash": project_path_hash(self.project),
            "port": 6512,
            "bridge_version": "0.5.0",
            "protocol_version": "1",
            "heartbeat_unix_ms": int(time.time() * 1000),
        }
        record.update(changes)
        self.path.write_text(json.dumps(record), encoding="utf-8")

    def test_live_record_supplies_project_port(self) -> None:
        self.write_record()
        self.assertEqual(discovered_port(self.project, 6505), 6512)
        self.assertEqual(read_discovery_record(self.project).port, 6512)  # type: ignore[union-attr]

    def test_stale_record_falls_back(self) -> None:
        self.write_record(heartbeat_unix_ms=int((time.time() - 30) * 1000))
        self.assertEqual(discovered_port(self.project, 6505), 6505)

    def test_malformed_record_falls_back(self) -> None:
        self.path.write_text("not json", encoding="utf-8")
        self.assertEqual(discovered_port(self.project, 6505), 6505)

    def test_other_project_record_is_rejected(self) -> None:
        self.write_record(project_hash="f" * 64)
        with self.assertRaises(ProjectMismatchError):
            discovered_port(self.project, 6505)

    def test_project_identity_normalization_covers_platform_branches(self) -> None:
        resolved = str(self.project).replace("\\", "/").rstrip("/")
        with patch("godot_editor_mcp.discovery.os.name", "posix"):
            self.assertEqual(normalized_project_path(self.project), resolved)
        with patch("godot_editor_mcp.discovery.os.name", "nt"):
            self.assertEqual(normalized_project_path(self.project), resolved.lower())


if __name__ == "__main__":
    unittest.main()
