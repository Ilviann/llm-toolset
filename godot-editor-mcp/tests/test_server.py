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


class FakeAssets:
    def __init__(self) -> None:
        self.calls: list[tuple[str, object]] = []

    def import_asset(self, source, destination):
        self.calls.append(("import", (source, destination)))
        return {"destination": f"res://{destination}", "bytes": 4}

    def create_folder(self, path):
        self.calls.append(("folder", path))
        return {"path": f"res://{path}", "created": True}

    def validate_folder(self, path):
        self.calls.append(("validate_folder", path))

    def validate_file(self, path, extensions=None):
        self.calls.append(("validate_file", (path, extensions)))

    def validate_new_file(self, path, extensions):
        self.calls.append(("validate_new", (path, extensions)))


class MCPServerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.bridge = FakeBridge()
        self.assets = FakeAssets()
        self.server = MCPServer(  # type: ignore[arg-type]
            self.bridge, self.assets
        )

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
            "editor_state", "list_assets", "asset_info", "import_asset",
            "create_folder", "create_resource", "create_scene", "open_scene", "scene_tree",
            "add_node", "instantiate_scene", "node_info", "set_property",
            "select_node", "scene_control",
        ])

    def test_tool_maps_to_short_plugin_command(self) -> None:
        response = self.request("tools/call", {
            "name": "select_node", "arguments": {"path": "Player"}
        })
        self.assertNotIn("isError", response["result"])
        self.assertEqual(self.bridge.calls, [("select", {"path": "Player"})])

    def test_import_copies_then_queues_editor_scan(self) -> None:
        response = self.request("tools/call", {
            "name": "import_asset",
            "arguments": {"source": "hero.png", "destination": "assets/hero.png"},
        })
        self.assertNotIn("isError", response["result"])
        self.assertEqual(self.assets.calls, [
            ("import", ("hero.png", "assets/hero.png"))
        ])
        self.assertEqual(self.bridge.calls, [
            ("scan_asset", {"path": "assets/hero.png"})
        ])

    def test_create_scene_is_validated_then_sent_to_editor(self) -> None:
        arguments = {"path": "scenes/main.tscn", "root_type": "Node2D", "root_name": "Main"}
        response = self.request("tools/call", {"name": "create_scene", "arguments": arguments})
        self.assertNotIn("isError", response["result"])
        self.assertEqual(self.assets.calls, [
            ("validate_new", ("scenes/main.tscn", {".tscn"}))
        ])
        self.assertEqual(self.bridge.calls, [("create_scene", arguments)])

    def test_create_resource_is_validated_then_sent_to_editor(self) -> None:
        arguments = {
            "path": "materials/red.tres", "type": "StandardMaterial3D",
            "properties": {"metallic": 0.4},
        }
        response = self.request("tools/call", {
            "name": "create_resource", "arguments": arguments
        })
        self.assertNotIn("isError", response["result"])
        self.assertEqual(self.assets.calls, [
            ("validate_new", ("materials/red.tres", {".tres"}))
        ])
        self.assertEqual(self.bridge.calls, [("create_resource", arguments)])

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
