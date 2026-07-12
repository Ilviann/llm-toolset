from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from godot_editor_mcp.bridge import BridgeError, GodotBridge


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

    def test_plugin_error_is_safe(self) -> None:
        peer = FakeSocket(b'{"ok":false,"error":"No scene is open"}\n')
        with patch("socket.create_connection", return_value=peer):
            with self.assertRaisesRegex(BridgeError, "No scene is open"):
                GodotBridge(self.root).call("tree")

    def test_project_must_contain_godot_file(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaisesRegex(BridgeError, "project.godot"):
                GodotBridge(folder)


if __name__ == "__main__":
    unittest.main()
