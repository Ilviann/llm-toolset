from __future__ import annotations

import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

from godot_editor_mcp import __version__
from godot_editor_mcp.bridge import GodotBridge
from godot_editor_mcp.discovery import read_discovery_record
from godot_editor_mcp.errors import (
    EditorBusyError,
    InvalidArgumentError,
    NoActiveRunError,
    RuntimeProbeUnavailableError,
    SaveFailedError,
    StaleCursorError,
    StaleRuntimeIdError,
)
from godot_editor_mcp.tool_dispatch import ToolDispatcher
from godot_editor_mcp.tool_catalog import bridge_contract_mismatches
from godot_editor_mcp.waiting import OperationWaiter


RUN_INTEGRATION = os.environ.get("GODOT_RELOAD_INTEGRATION") == "1"


@unittest.skipUnless(
    RUN_INTEGRATION and sys.platform == "darwin",
    "set GODOT_RELOAD_INTEGRATION=1 on the verified macOS platform",
)
class ReloadSubprocessIntegrationTests(unittest.TestCase):
    def setUp(self) -> None:
        executable = os.environ.get(
            "GODOT_EXECUTABLE", "/Applications/Godot.app/Contents/MacOS/Godot"
        )
        self.executable = Path(executable)
        if not self.executable.is_file():
            self.skipTest("Godot executable is unavailable")
        self.temp = tempfile.TemporaryDirectory()
        self.project = Path(self.temp.name)
        shutil.copytree(
            Path(__file__).parents[1] / "plugin" / "addons",
            self.project / "addons",
        )
        (self.project / "scenes").mkdir()
        (self.project / "scenes" / "runtime.gd").write_text(
            "extends Node2D\n\n"
            "func _ready():\n"
            "    var spawned = Node2D.new()\n"
            '    spawned.name = "SpawnedAtRuntime"\n'
            "    spawned.process_priority = 73\n"
            '    spawned.add_to_group("runtime_enemies")\n'
            "    add_child(spawned)\n",
            encoding="utf-8",
        )
        (self.project / "scenes" / "runtime.tscn").write_text(
            '[gd_scene load_steps=2 format=3]\n\n'
            '[ext_resource path="res://scenes/runtime.gd" type="Script" id="1"]\n\n'
            '[node name="RuntimeMain" type="Node2D"]\n'
            'script = ExtResource("1")\n',
            encoding="utf-8",
        )
        assets = self.project / "assets"
        assets.mkdir()
        for name in ("one", "two", "three"):
            (assets / f"{name}.gd").write_text(
                f"extends Node\n# {name}\n", encoding="utf-8"
            )
        self.port = self._free_port()
        (self.project / "project.godot").write_text(
            "; Phase 3 reload integration fixture.\n"
            "config_version=5\n\n"
            "[application]\n"
            'config/name="Godot MCP Reload Integration"\n'
            'config/features=PackedStringArray("4.7")\n\n'
            "[editor_plugins]\n"
            'enabled=PackedStringArray("res://addons/godot_mcp/plugin.cfg")\n\n'
            "[godot_mcp]\n"
            f"port={self.port}\n\n"
            "[rendering]\n"
            'renderer/rendering_method="gl_compatibility"\n',
            encoding="utf-8",
        )
        self.log = (self.project / "editor.log").open("w+", encoding="utf-8")
        self.process = subprocess.Popen(
            [
                str(self.executable),
                "--headless",
                "--editor",
                "--path",
                str(self.project),
                "--log-file",
                str(self.project / "godot.log"),
            ],
            stdin=subprocess.DEVNULL,
            stdout=self.log,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        self.started_pids: set[int] = {self.process.pid}
        self._wait_for_bridge()

    def tearDown(self) -> None:
        try:
            record = read_discovery_record(self.project)
            if record is not None:
                self.started_pids.add(record.process_id)
        except Exception:
            pass
        for process_id in self.started_pids:
            try:
                os.kill(process_id, signal.SIGTERM)
            except ProcessLookupError:
                pass
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
        self.log.close()
        self.temp.cleanup()

    def test_reload_safeguards_and_authenticated_reconnect(self) -> None:
        bridge = GodotBridge(self.project, timeout=0.5)
        self.assertEqual(
            bridge_contract_mismatches(
                bridge.call("capabilities", {}), expected_version=__version__
            ),
            [],
        )
        created = bridge.call(
            "create_scene",
            {"path": "scenes/main.tscn", "root_type": "Node2D", "root_name": "Main"},
        )
        self.assertEqual(created["path"], "res://scenes/main.tscn")
        bridge.call("open_scene", {"path": "scenes/main.tscn"})
        self._wait_until(lambda: bridge.call("state", {}).get("scene") == "res://scenes/main.tscn")

        bridge.call("open_scene", {"path": "scenes/runtime.tscn"})
        self._wait_until(
            lambda: bridge.call("state", {}).get("scene") == "res://scenes/runtime.tscn"
        )
        with self.assertRaises(NoActiveRunError):
            bridge.call("tree", {"tree_scope": "runtime"})
        first_run = bridge.call("control", {"action": "run"})
        self._wait_until(lambda: bridge.call("state", {}).get("playing") is True)
        runtime_tree = self._runtime_call(
            bridge, "tree", {"tree_scope": "runtime", "max_depth": 3, "limit": 1}
        )
        self.assertEqual(runtime_tree["scope"], "runtime")
        self.assertEqual(runtime_tree["run_id"], first_run["run_id"])
        self.assertTrue(runtime_tree["continuation_available"])
        runtime_cursor = runtime_tree["cursor"]
        runtime_next = self._runtime_call(
            bridge,
            "tree",
            {
                "tree_scope": "runtime", "max_depth": 3, "limit": 1,
                "cursor": runtime_cursor,
            },
        )
        spawned = runtime_next["nodes"][0]
        self.assertEqual(spawned["path"], "SpawnedAtRuntime")
        self.assertEqual(spawned["groups"], ["runtime_enemies"])
        self.assertEqual(len(spawned["runtime_id"]), 64)
        runtime_info = self._runtime_call(
            bridge,
            "inspect",
            {
                "tree_scope": "runtime", "path": "SpawnedAtRuntime",
                "runtime_id": spawned["runtime_id"], "property": "process_priority",
            },
        )
        self.assertEqual(runtime_info["scope"], "runtime")
        self.assertEqual(runtime_info["properties"][0]["value"], 73)
        bridge.call("control", {"action": "stop", "run_id": first_run["run_id"]})
        self._wait_until(lambda: bridge.call("state", {}).get("playing") is False)
        second_run = bridge.call("control", {"action": "run"})
        self._wait_until(lambda: bridge.call("state", {}).get("playing") is True)
        self._runtime_call(bridge, "tree", {"tree_scope": "runtime", "limit": 1})
        with self.assertRaises(StaleRuntimeIdError):
            bridge.call(
                "inspect",
                {
                    "tree_scope": "runtime", "path": "SpawnedAtRuntime",
                    "runtime_id": spawned["runtime_id"],
                },
            )
        with self.assertRaises(StaleCursorError):
            bridge.call(
                "tree",
                {
                    "tree_scope": "runtime", "max_depth": 3, "limit": 1,
                    "cursor": runtime_cursor,
                },
            )
        bridge.call("control", {"action": "stop", "run_id": second_run["run_id"]})
        self._wait_until(lambda: bridge.call("state", {}).get("playing") is False)
        bridge.call("open_scene", {"path": "scenes/main.tscn"})
        self._wait_until(
            lambda: bridge.call("state", {}).get("scene") == "res://scenes/main.tscn"
        )

        asset_page = bridge.call(
            "assets", {"folder": "assets", "type": "script", "limit": 1}
        )
        self.assertTrue(asset_page["truncated"])
        self.assertTrue(asset_page["continuation_available"])
        self.assertEqual(len(asset_page["assets"]), 1)
        asset_cursor = asset_page["cursor"]
        next_assets = bridge.call(
            "assets",
            {
                "folder": "assets", "type": "script", "limit": 1,
                "cursor": asset_cursor,
            },
        )
        self.assertEqual(asset_page["snapshot_id"], next_assets["snapshot_id"])
        self.assertNotEqual(
            asset_page["assets"][0]["path"], next_assets["assets"][0]["path"]
        )
        asset_paths = [asset_page["assets"][0]["path"]]
        current_assets = next_assets
        while True:
            asset_paths.extend(item["path"] for item in current_assets["assets"])
            if not current_assets["continuation_available"]:
                break
            current_assets = bridge.call(
                "assets",
                {
                    "folder": "assets", "type": "script", "limit": 1,
                    "cursor": current_assets["cursor"],
                },
            )
            self.assertEqual(asset_page["snapshot_id"], current_assets["snapshot_id"])
        self.assertEqual(
            set(asset_paths),
            {"res://assets/one.gd", "res://assets/two.gd", "res://assets/three.gd"},
        )
        self.assertEqual(len(asset_paths), len(set(asset_paths)))
        with self.assertRaises(InvalidArgumentError):
            bridge.call(
                "assets",
                {"folder": "assets", "type": "all", "limit": 1,
                 "cursor": asset_cursor},
            )
        generation = bridge.call("state", {})["filesystem_generation"]
        bridge.call(
            "create_resource",
            {"path": "assets/generated.tres", "type": "Gradient"},
        )
        self._wait_until(
            lambda: bridge.call("state", {})["filesystem_generation"] > generation
        )
        with self.assertRaises(StaleCursorError):
            bridge.call(
                "assets",
                {"folder": "assets", "type": "script", "limit": 1,
                 "cursor": asset_cursor},
            )

        for name in ("PageA", "PageB", "PageC"):
            bridge.call("add_node", {"parent": ".", "type": "Node2D", "name": name})
        tree_page = bridge.call("tree", {"max_depth": 4, "limit": 2})
        self.assertEqual(tree_page["scope"], "edited")
        self.assertTrue(tree_page["continuation_available"])
        tree_cursor = tree_page["cursor"]
        next_tree = bridge.call(
            "tree", {"max_depth": 4, "limit": 2, "cursor": tree_cursor}
        )
        self.assertEqual(tree_page["snapshot_id"], next_tree["snapshot_id"])
        self.assertTrue(
            {node["path"] for node in tree_page["nodes"]}.isdisjoint(
                node["path"] for node in next_tree["nodes"]
            )
        )
        self.assertEqual(
            {
                node["path"]
                for node in tree_page["nodes"] + next_tree["nodes"]
            },
            {".", "PageA", "PageB", "PageC"},
        )
        targeted = bridge.call(
            "tree", {"root": "PageA", "max_depth": 0, "class": "Node2D"}
        )
        self.assertEqual([node["path"] for node in targeted["nodes"]], ["PageA"])
        bridge.call("add_node", {"parent": ".", "type": "Node", "name": "NewEdit"})
        with self.assertRaises(StaleCursorError):
            bridge.call(
                "tree", {"max_depth": 4, "limit": 2, "cursor": tree_cursor}
            )

        property_page = bridge.call("inspect", {"path": ".", "limit": 1})
        self.assertTrue(property_page["continuation_available"])
        self.assertIn("category", property_page["properties"][0])
        property_cursor = property_page["cursor"]
        next_properties = bridge.call(
            "inspect", {"path": ".", "limit": 1, "cursor": property_cursor}
        )
        self.assertNotEqual(
            property_page["properties"][0]["name"],
            next_properties["properties"][0]["name"],
        )
        first_property = property_page["properties"][0]
        filtered = bridge.call(
            "inspect",
            {
                "path": ".", "property": first_property["name"],
                "category": first_property["category"],
            },
        )
        self.assertEqual(len(filtered["properties"]), 1)
        bridge.call(
            "create_scene",
            {"path": "scenes/other.tscn", "root_type": "Node2D", "root_name": "Other"},
        )
        bridge.call("open_scene", {"path": "scenes/other.tscn"})
        self._wait_until(
            lambda: bridge.call("state", {}).get("scene") == "res://scenes/other.tscn"
        )
        with self.assertRaises(StaleCursorError):
            bridge.call(
                "inspect", {"path": ".", "limit": 1, "cursor": property_cursor}
            )
        bridge.call("open_scene", {"path": "scenes/main.tscn"})
        self._wait_until(lambda: bridge.call("state", {}).get("scene") == "res://scenes/main.tscn")

        run = bridge.call("control", {"action": "run"})
        self._wait_until(lambda: bridge.call("state", {}).get("playing") is True)
        with self.assertRaises(EditorBusyError) as active:
            bridge.call("reload_project", {})
        self.assertIn("running", active.exception.message)
        bridge.call(
            "add_node", {"parent": ".", "type": "Node2D", "name": "UnsavedChild"}
        )
        with self.assertRaises(EditorBusyError) as dirty:
            bridge.call("reload_project", {"stop_running": True})
        self.assertIn("Unsaved", dirty.exception.message)
        self._wait_until(lambda: bridge.call("state", {}).get("playing") is False)

        scene_path = self.project / "scenes" / "main.tscn"
        scenes_path = scene_path.parent
        scene_path.chmod(0o444)
        scenes_path.chmod(0o555)
        try:
            with self.assertRaises(SaveFailedError) as save_failed:
                bridge.call("reload_project", {"save_scenes": True})
            self.assertIn("could not be saved", save_failed.exception.message)
        finally:
            scenes_path.chmod(0o755)
            scene_path.chmod(0o644)

        old_record = read_discovery_record(self.project)
        assert old_record is not None
        dispatcher = ToolDispatcher(
            bridge, None, mode="tiny", launcher=None, waiter=OperationWaiter(bridge)
        )
        result = dispatcher.call(
            "reload_project",
            {"save_scenes": True, "wait": True, "timeout_ms": 30_000},
        )
        wait = result["wait"]
        self.assertTrue(wait["completed"])
        self.assertTrue(wait["disconnected"])
        self.assertEqual(wait["operation_id"], result["operation_id"])
        self.assertEqual(wait["project_hash"], result["project_hash"])
        self.assertEqual(wait["bridge_version"], result["bridge_version"])
        self._wait_until(
            lambda: not (self.project / ".godot" / "godot_mcp_reload.json").exists()
        )

        new_record = read_discovery_record(self.project)
        assert new_record is not None
        self.started_pids.add(new_record.process_id)
        self.assertNotEqual(new_record.process_id, old_record.process_id)
        self.assertEqual(new_record.project_hash, old_record.project_hash)
        self._wait_until(
            lambda: bridge.call("state", {}).get("scene")
            == "res://scenes/main.tscn"
        )
        reloaded_capabilities = bridge.call("capabilities", {})
        self.assertTrue(reloaded_capabilities["features"]["runtime_inspection"])
        self.assertTrue(reloaded_capabilities["runtime_probe"]["available"])
        tree = bridge.call("tree", {})
        self.assertTrue(any(node["path"] == "UnsavedChild" for node in tree["nodes"]))

    def _wait_for_bridge(self) -> None:
        self._wait_until(
            lambda: (
                (record := read_discovery_record(self.project)) is not None
                and record.port == self.port
                and record.is_live()
            ),
            timeout=15,
        )

    @staticmethod
    def _free_port() -> int:
        with socket.socket() as peer:
            peer.bind(("127.0.0.1", 0))
            return int(peer.getsockname()[1])

    def _wait_until(self, predicate, *, timeout: float = 10.0) -> None:
        deadline = time.monotonic() + timeout
        last_error: Exception | None = None
        while time.monotonic() < deadline:
            try:
                if predicate():
                    return
            except Exception as exc:
                last_error = exc
            time.sleep(0.05)
        self.log.flush()
        self.log.seek(0)
        output = self.log.read()[-4000:]
        self.fail(f"condition timed out; last_error={last_error!r}\n{output}")

    def _runtime_call(
        self, bridge: GodotBridge, command: str, arguments: dict
    ) -> dict:
        deadline = time.monotonic() + 5
        last_error: Exception | None = None
        while time.monotonic() < deadline:
            try:
                result = bridge.call(command, arguments)
                assert isinstance(result, dict)
                return result
            except RuntimeProbeUnavailableError as exc:
                last_error = exc
                time.sleep(0.05)
        self.log.flush()
        self.log.seek(0)
        output = self.log.read()[-4000:]
        capabilities = bridge.call("capabilities", {})
        self.fail(
            "runtime probe handshake timed out; "
            f"last_error={last_error!r}; status={capabilities.get('runtime_probe')}\n"
            f"{output}"
        )


if __name__ == "__main__":
    unittest.main()
