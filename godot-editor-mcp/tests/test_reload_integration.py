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
from godot_editor_mcp.errors import EditorBusyError, SaveFailedError
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


if __name__ == "__main__":
    unittest.main()
