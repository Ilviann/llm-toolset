from __future__ import annotations

import json
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

from godot_editor_mcp.bridge import BridgeError, GodotBridge
from godot_editor_mcp.discovery import DISCOVERY_FILE, project_path_hash
from godot_editor_mcp.errors import NotFoundError


class FakeSocket:
    def __init__(self, response: bytes) -> None:
        self.response = response
        self.sent = b""

    def __enter__(self):
        return self

    def __exit__(self, *args):
        return None

    def settimeout(self, _timeout: float) -> None:
        pass

    def sendall(self, data: bytes) -> None:
        self.sent += data

    def recv(self, _size: int) -> bytes:
        response, self.response = self.response, b""
        return response


class BridgeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / "project.godot").write_text("[application]\n", encoding="utf-8")
        (self.root / ".godot").mkdir()
        (self.root / ".godot" / "godot_mcp_token").write_text("a" * 64, encoding="ascii")

    def tearDown(self) -> None:
        self.temp.cleanup()

    def test_authenticated_call(self) -> None:
        peer = FakeSocket(b'{"ok":true,"result":{"playing":false}}\n')
        with patch("socket.create_connection", return_value=peer):
            result = GodotBridge(self.root).call("state")
        self.assertEqual(result, {"playing": False})
        request = json.loads(peer.sent)
        self.assertEqual(request["token"], "a" * 64)
        self.assertEqual(request["command"], "state")

    def test_live_project_discovery_selects_port(self) -> None:
        discovery = {
            "process_id": 123,
            "project_hash": project_path_hash(self.root),
            "port": 6512,
            "bridge_version": "0.5.0",
            "protocol_version": "1",
            "heartbeat_unix_ms": int(time.time() * 1000),
        }
        (self.root / ".godot" / DISCOVERY_FILE).write_text(
            json.dumps(discovery), encoding="utf-8"
        )
        peer = FakeSocket(b'{"ok":true,"result":{}}\n')
        with patch("socket.create_connection", return_value=peer) as connect:
            GodotBridge(self.root).call("state")
        connect.assert_called_once_with(("127.0.0.1", 6512), 3.0)

    def test_plugin_error_is_safe(self) -> None:
        peer = FakeSocket(b'{"ok":false,"error":"No scene is open"}\n')
        with patch("socket.create_connection", return_value=peer):
            with self.assertRaisesRegex(BridgeError, "No scene is open"):
                GodotBridge(self.root).call("tree")

    def test_structured_plugin_error_preserves_public_fields_and_type(self) -> None:
        peer = FakeSocket(
            b'{"ok":false,"error":{"code":"not_found","message":"Node not found",'
            b'"details":{"path":"Missing"},"retryable":false}}\n'
        )
        with patch("socket.create_connection", return_value=peer):
            with self.assertRaises(NotFoundError) as raised:
                GodotBridge(self.root).call("tree")
        self.assertEqual(raised.exception.code, "not_found")
        self.assertEqual(raised.exception.details, {"path": "Missing"})
        self.assertFalse(raised.exception.retryable)

    def test_project_must_contain_godot_file(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaisesRegex(BridgeError, "project.godot"):
                GodotBridge(folder)


if __name__ == "__main__":
    unittest.main()
