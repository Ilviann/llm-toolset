from __future__ import annotations

import base64
import json
import os
import tempfile
import unittest
from pathlib import Path

from godot_editor_mcp import __version__
from godot_editor_mcp.bridge import BridgeError
from godot_editor_mcp.errors import AssetError, NotFoundError
from godot_editor_mcp.server import MCPServer, MODE_TOOL_NAMES


class FakeBridge:
    def __init__(self) -> None:
        self.calls: list[tuple[str, dict]] = []

    def call(self, command: str, arguments: dict | None = None):
        self.calls.append((command, arguments or {}))
        if command == "inspect" and arguments == {"path": "Missing"}:
            raise NotFoundError("Node not found", details={"path": "Missing"})
        return {"command": command, "arguments": arguments or {}}


class FakeAssets:
    def __init__(self) -> None:
        self.calls: list[tuple[str, object]] = []
        self.project = Path(".")

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


class FakeLauncher:
    configured = True

    def __init__(self) -> None:
        self.calls = 0

    def start(self, bridge):
        self.calls += 1
        return {"status": "started"}


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
        self.assertEqual(initialized["result"]["serverInfo"]["mode"], "tiny")
        names = [tool["name"] for tool in self.request("tools/list")["result"]["tools"]]
        self.assertEqual(names, list(MODE_TOOL_NAMES["tiny"]))

    def test_waiter_factory_is_composed_outside_dispatcher(self) -> None:
        created: list[object] = []

        class FakeWaiter:
            def cancel(self) -> None:
                created.append("cancelled")

        def factory(bridge):
            created.append(bridge)
            return FakeWaiter()

        server = MCPServer(  # type: ignore[arg-type]
            self.bridge, self.assets, waiter_factory=factory
        )
        self.assertEqual(created, [self.bridge])
        server.close()
        self.assertEqual(created, [self.bridge, "cancelled"])

    def test_modes_have_separate_nested_toolsets(self) -> None:
        tiny = set(MODE_TOOL_NAMES["tiny"])
        small = set(MODE_TOOL_NAMES["small"])
        large = set(MODE_TOOL_NAMES["large"])
        self.assertLess(tiny, small)
        self.assertLess(small, large)
        self.assertNotIn("list_assets", tiny)
        self.assertIn("list_assets", small)
        self.assertNotIn("select_node", small)
        self.assertIn("select_node", large)
        self.assertNotIn("start_editor", small)
        self.assertIn("start_editor", large)
        self.assertNotIn("project_settings_get", tiny)
        self.assertIn("project_settings_get", small)
        self.assertIn("project_settings_patch", small)
        self.assertIn("input_map_patch", small)
        self.assertIn("reload_project", tiny)
        self.assertNotIn("capture_game_view", tiny)
        self.assertIn("capture_game_view", small)
        self.assertIn("send_input", small)
        self.assertIn("wait_for_runtime_condition", small)
        self.assertNotIn("scene_transaction", tiny)
        self.assertIn("scene_transaction", small)

    def test_tool_outside_mode_is_rejected_without_bridge_call(self) -> None:
        response = self.request("tools/call", {
            "name": "import_asset", "arguments": {
                "source": "hero.png", "destination": "assets/hero.png",
            },
        })
        self.assertEqual(response["error"]["code"], -32602)
        self.assertIn("tiny mode", response["error"]["message"])
        self.assertEqual(self.bridge.calls, [])
        self.assertEqual(self.assets.calls, [])

    def test_tool_maps_to_short_plugin_command(self) -> None:
        self.server = MCPServer(self.bridge, self.assets, mode="large")  # type: ignore[arg-type]
        response = self.request("tools/call", {
            "name": "select_node", "arguments": {"path": "Player"}
        })
        self.assertNotIn("isError", response["result"])
        self.assertEqual(self.bridge.calls, [("select", {"path": "Player"})])

    def test_diagnostics_is_available_in_every_mode(self) -> None:
        response = self.request("tools/call", {
            "name": "get_diagnostics", "arguments": {
                "scope": "parser", "severity": "error", "since": 4, "limit": 10,
            },
        })
        self.assertNotIn("isError", response["result"])
        self.assertEqual(self.bridge.calls, [("diagnostics", {
            "scope": "parser", "severity": "error", "since": 4, "limit": 10,
        })])

    def test_open_scene_wait_fields_are_not_sent_to_plugin(self) -> None:
        class CompletedBridge(FakeBridge):
            def call(self, command: str, arguments: dict | None = None):
                self.calls.append((command, arguments or {}))
                if command == "open_scene":
                    return {"operation_id": "op-1", "open": "requested"}
                if command == "state":
                    return {
                        "scene": "res://scenes/main.tscn",
                        "active_operations": [],
                        "last_diagnostic_id": None,
                        "bridge_version": __version__,
                    }
                return {"command": command, "arguments": arguments or {}}

        bridge = CompletedBridge()
        self.server = MCPServer(bridge, self.assets)  # type: ignore[arg-type]
        response = self.request("tools/call", {
            "name": "open_scene", "arguments": {
                "path": "scenes/main.tscn", "wait": True, "timeout_ms": 1000,
            },
        })
        self.assertNotIn("isError", response["result"])
        self.assertEqual(bridge.calls[0], ("open_scene", {"path": "scenes/main.tscn"}))

    def test_reload_wait_fields_stay_local_and_reconnect_is_reported(self) -> None:
        class ReloadedBridge(FakeBridge):
            def call(self, command: str, arguments: dict | None = None):
                self.calls.append((command, arguments or {}))
                if command == "reload_project":
                    return {
                        "status": "scheduled",
                        "operation_id": "op-9",
                        "project_hash": "a" * 64,
                        "bridge_version": __version__,
                    }
                if command == "reload_status":
                    return {
                        "completed": True,
                        "status": "completed",
                        "operation_id": "op-9",
                        "project_hash": "a" * 64,
                        "bridge_version": __version__,
                    }
                raise AssertionError(command)

        bridge = ReloadedBridge()
        self.server = MCPServer(bridge, self.assets)  # type: ignore[arg-type]
        response = self.request("tools/call", {
            "name": "reload_project",
            "arguments": {
                "stop_running": True,
                "save_scenes": True,
                "wait": True,
                "timeout_ms": 1000,
            },
        })
        self.assertNotIn("isError", response["result"])
        self.assertEqual(bridge.calls[0], ("reload_project", {
            "stop_running": True, "save_scenes": True,
        }))

    def test_import_copies_then_queues_editor_scan(self) -> None:
        self.server = MCPServer(self.bridge, self.assets, mode="small")  # type: ignore[arg-type]
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
        payload = json.loads(response["result"]["content"][0]["text"])
        self.assertIn("operation_id", payload)

    def test_create_scene_is_validated_then_sent_to_editor(self) -> None:
        arguments = {"path": "scenes/main.tscn", "root_type": "Node2D", "root_name": "Main"}
        response = self.request("tools/call", {"name": "create_scene", "arguments": arguments})
        self.assertNotIn("isError", response["result"])
        self.assertEqual(self.assets.calls, [
            ("validate_new", ("scenes/main.tscn", {".tscn"}))
        ])
        self.assertEqual(self.bridge.calls, [("create_scene", arguments)])

    def test_runtime_inspection_scope_and_identity_reach_existing_routes(self) -> None:
        tree_arguments = {
            "tree_scope": "runtime", "root": "Enemies", "limit": 5,
        }
        info_arguments = {
            "tree_scope": "runtime", "path": "Enemies/Slime",
            "runtime_id": "a" * 64, "property": "health",
        }
        for name, arguments in (
            ("scene_tree", tree_arguments), ("node_info", info_arguments)
        ):
            response = self.request(
                "tools/call", {"name": name, "arguments": arguments}
            )
            self.assertNotIn("isError", response["result"])
        self.assertEqual(self.bridge.calls, [
            ("tree", tree_arguments), ("inspect", info_arguments),
        ])

    def test_gameplay_tools_route_with_explicit_run_identity(self) -> None:
        self.server = MCPServer(self.bridge, self.assets, mode="small")  # type: ignore[arg-type]
        calls = [
            ("send_input", {
                "run_id": 4, "action": "jump", "strength": 0.5, "frames": 2,
            }),
            ("wait_for_runtime_condition", {
                "scope": "runtime", "run_id": 4, "condition": "node_exists",
                "path": "Enemies/Boss", "exists": True, "timeout_ms": 500,
            }),
        ]
        for name, arguments in calls:
            response = self.request("tools/call", {"name": name, "arguments": arguments})
            self.assertNotIn("isError", response["result"])
        self.assertEqual(self.bridge.calls, [
            ("send_input", calls[0][1]),
            ("wait_runtime_condition", calls[1][1]),
        ])

    def test_scene_transaction_routes_only_in_small_and_large_modes(self) -> None:
        self.server = MCPServer(self.bridge, self.assets, mode="small")  # type: ignore[arg-type]
        arguments = {
            "preconditions": {"scene": "res://main.tscn", "undo_version": 4},
            "operations": [{
                "op": "rename_node", "target": {"path": "Player"},
                "name": "Hero", "handle": "hero",
            }],
        }
        response = self.request("tools/call", {
            "name": "scene_transaction", "arguments": arguments,
        })
        self.assertNotIn("isError", response["result"])
        self.assertEqual(self.bridge.calls, [("scene_transaction", arguments)])

    def test_capture_is_validated_returned_as_image_and_deleted(self) -> None:
        png = base64.b64decode(
            "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJ"
            "AAAADUlEQVR42mNk+M/wHwAF/gL+MZ7Z4QAAAABJRU5ErkJggg=="
        )
        capture_id = "a" * 32
        with tempfile.TemporaryDirectory() as folder:
            project = Path(folder)
            capture_folder = project / ".godot" / "godot_mcp" / "captures"
            capture_folder.mkdir(parents=True)
            capture_path = capture_folder / f"{capture_id}.png"

            class CaptureBridge(FakeBridge):
                def call(self, command: str, arguments: dict | None = None):
                    self.calls.append((command, arguments or {}))
                    capture_path.write_bytes(png)
                    return {
                        "capture_id": capture_id, "run_id": 4,
                        "source_width": 1, "source_height": 1,
                        "width": 1, "height": 1, "bytes": len(png),
                        "format": "png",
                    }

            bridge = CaptureBridge()
            self.assets.project = project
            self.server = MCPServer(bridge, self.assets, mode="small")  # type: ignore[arg-type]
            response = self.request("tools/call", {
                "name": "capture_game_view", "arguments": {"run_id": 4},
            })
            content = response["result"]["content"]
            self.assertEqual([item["type"] for item in content], ["text", "image"])
            self.assertEqual(base64.b64decode(content[1]["data"]), png)
            self.assertEqual(content[1]["mimeType"], "image/png")
            self.assertFalse(capture_path.exists())
            self.assertEqual(bridge.calls, [("capture_game_view", {"run_id": 4})])

    def test_capture_folder_symlink_cannot_read_or_delete_outside_project(self) -> None:
        png = base64.b64decode(
            "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJ"
            "AAAADUlEQVR42mNk+M/wHwAF/gL+MZ7Z4QAAAABJRU5ErkJggg=="
        )
        capture_id = "b" * 32
        with tempfile.TemporaryDirectory() as folder, tempfile.TemporaryDirectory() as outside:
            project = Path(folder)
            staging_parent = project / ".godot" / "godot_mcp"
            staging_parent.mkdir(parents=True)
            outside_path = Path(outside) / f"{capture_id}.png"
            outside_path.write_bytes(png)
            try:
                os.symlink(outside, staging_parent / "captures", target_is_directory=True)
            except (OSError, NotImplementedError):
                self.skipTest("directory symlinks are unavailable")

            class UnsafeCaptureBridge(FakeBridge):
                def call(self, command: str, arguments: dict | None = None):
                    return {
                        "capture_id": capture_id, "run_id": 4,
                        "source_width": 1, "source_height": 1,
                        "width": 1, "height": 1, "bytes": len(png),
                        "format": "png",
                    }

            self.assets.project = project
            self.server = MCPServer(  # type: ignore[arg-type]
                UnsafeCaptureBridge(), self.assets, mode="small"
            )
            response = self.request("tools/call", {
                "name": "capture_game_view", "arguments": {"run_id": 4},
            })
            self.assertTrue(response["result"]["isError"])
            self.assertTrue(outside_path.exists())

    def test_create_resource_is_validated_then_sent_to_editor(self) -> None:
        self.server = MCPServer(self.bridge, self.assets, mode="small")  # type: ignore[arg-type]
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

    def test_public_scan_is_validated_then_sent_to_editor(self) -> None:
        self.server = MCPServer(self.bridge, self.assets, mode="small")  # type: ignore[arg-type]
        arguments = {"path": "scripts/player.gd"}
        response = self.request("tools/call", {
            "name": "scan_asset", "arguments": arguments,
        })
        self.assertNotIn("isError", response["result"])
        self.assertEqual(self.assets.calls, [
            ("validate_file", ("scripts/player.gd", None))
        ])
        self.assertEqual(self.bridge.calls, [("scan_asset", arguments)])

    def test_project_settings_tools_route_to_distinct_bridge_commands(self) -> None:
        self.server = MCPServer(self.bridge, self.assets, mode="small")  # type: ignore[arg-type]
        calls = [
            ("project_settings_get", {"key": "display/window/size"}),
            ("project_settings_patch", {
                "changes": [{"key": "display/window/stretch/mode", "value": "canvas_items"}],
                "dry_run": True,
            }),
            ("input_map_patch", {
                "action": "ui_accept",
                "add_events": [{"type": "joypad_button", "button": "a"}],
            }),
        ]
        for name, arguments in calls:
            response = self.request("tools/call", {"name": name, "arguments": arguments})
            self.assertNotIn("isError", response["result"])
        self.assertEqual(self.bridge.calls, calls)

    def test_capabilities_include_mode_and_exposed_tools(self) -> None:
        self.server = MCPServer(self.bridge, self.assets, mode="small")  # type: ignore[arg-type]
        self.request("initialize", {"protocolVersion": "2025-06-18"})
        response = self.request("tools/call", {"name": "capabilities"})
        payload = response["result"]["content"][0]["text"]
        capabilities = json.loads(payload)
        self.assertEqual(capabilities["mode"], "small")
        self.assertEqual(capabilities["tools"], list(MODE_TOOL_NAMES["small"]))
        self.assertEqual(capabilities["mcp_protocol_version"], "2025-06-18")
        self.assertNotIn("editor_launcher", capabilities)

    def test_start_editor_is_large_only_and_uses_launcher(self) -> None:
        launcher = FakeLauncher()
        self.server = MCPServer(
            self.bridge, self.assets, mode="large", launcher=launcher
        )  # type: ignore[arg-type]
        response = self.request("tools/call", {"name": "start_editor"})
        self.assertNotIn("isError", response["result"])
        self.assertEqual(launcher.calls, 1)
        self.assertEqual(self.bridge.calls, [])

    def test_large_capabilities_report_launcher_configuration(self) -> None:
        launcher = FakeLauncher()
        self.server = MCPServer(
            self.bridge, self.assets, mode="large", launcher=launcher
        )  # type: ignore[arg-type]
        response = self.request("tools/call", {"name": "capabilities"})
        payload = response["result"]["content"][0]["text"]
        capabilities = json.loads(payload)
        self.assertEqual(capabilities["editor_launcher"], {"configured": True})

    def test_bridge_error_is_tool_error(self) -> None:
        response = self.request("tools/call", {
            "name": "node_info", "arguments": {"path": "Missing"}
        })
        self.assertTrue(response["result"]["isError"])
        error = json.loads(response["result"]["content"][0]["text"])
        self.assertEqual(error, {
            "code": "not_found",
            "details": {"path": "Missing"},
            "message": "Node not found",
            "retryable": False,
        })

    def test_local_domain_error_is_structured(self) -> None:
        self.server = MCPServer(self.bridge, None, mode="small")  # type: ignore[arg-type]
        response = self.request("tools/call", {
            "name": "create_folder", "arguments": {"path": "assets/new"}
        })
        self.assertTrue(response["result"]["isError"])
        error = json.loads(response["result"]["content"][0]["text"])
        self.assertEqual(error["code"], AssetError.default_code)
        self.assertIn("unavailable", error["message"])

    def test_unexpected_programming_error_is_not_flattened(self) -> None:
        class BrokenAssets(FakeAssets):
            def validate_file(self, path, extensions=None):
                raise TypeError("private programming error")

        self.server = MCPServer(
            self.bridge, BrokenAssets(), mode="small"
        )  # type: ignore[arg-type]
        with self.assertRaisesRegex(TypeError, "private programming error"):
            self.request("tools/call", {
                "name": "asset_info", "arguments": {"path": "asset.tres"}
            })

    def test_notification_has_no_response(self) -> None:
        self.assertIsNone(self.server.handle({
            "jsonrpc": "2.0", "method": "notifications/initialized"
        }))


if __name__ == "__main__":
    unittest.main()
