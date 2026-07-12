from __future__ import annotations

import unittest

from godot_editor_mcp.bridge import BridgeError
from godot_editor_mcp.server import MCPServer


class FakeBridge:
    def __init__(self) -> None:
        self.calls: list[tuple[str, dict]] = []

    def call(self, command: str, arguments: dict | None = None):
        self.calls.append((command, arguments or {}))
        if command == "inspect" and arguments == {"path": "Missing"}:
            raise BridgeError("Node not found")
        return {"command": command, "arguments": arguments or {}}


class MCPServerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.bridge = FakeBridge()
        self.server = MCPServer(self.bridge)  # type: ignore[arg-type]

    def request(self, method: str, params: dict | None = None) -> dict:
        response = self.server.handle({
            "jsonrpc": "2.0", "id": 1, "method": method, "params": params or {}
        })
        assert response is not None
        return response

    def test_initialize_and_list_tools(self) -> None:
        initialized = self.request("initialize", {"protocolVersion": "2025-06-18"})
        self.assertEqual(initialized["result"]["protocolVersion"], "2025-06-18")
        names = [tool["name"] for tool in self.request("tools/list")["result"]["tools"]]
        self.assertEqual(names, [
            "editor_state", "scene_tree", "node_info", "set_property",
            "select_node", "scene_control",
        ])

    def test_tool_maps_to_short_plugin_command(self) -> None:
        response = self.request("tools/call", {
            "name": "select_node", "arguments": {"path": "Player"}
        })
        self.assertNotIn("isError", response["result"])
        self.assertEqual(self.bridge.calls, [("select", {"path": "Player"})])

    def test_bridge_error_is_tool_error(self) -> None:
        response = self.request("tools/call", {
            "name": "node_info", "arguments": {"path": "Missing"}
        })
        self.assertTrue(response["result"]["isError"])
        self.assertEqual(response["result"]["content"][0]["text"], "Node not found")

    def test_notification_has_no_response(self) -> None:
        self.assertIsNone(self.server.handle({
            "jsonrpc": "2.0", "method": "notifications/initialized"
        }))


if __name__ == "__main__":
    unittest.main()
