import json
import tempfile
import unittest
from pathlib import Path

import unreal_editor_mcp
from unreal_editor_mcp.errors import BridgeError, ConfigurationError, ErrorCode
from unreal_editor_mcp.lifecycle import EditorLifecycle, resolve_editor_executable
from unreal_editor_mcp.platforms import PlatformAdapter
from unreal_editor_mcp.project import ProjectLayout
from unreal_editor_mcp.server import MCPServer


class FakeProcess:
    def __init__(self, pid, on_poll=None, return_code=None):
        self.pid = pid
        self._on_poll = on_poll
        self.return_code = return_code

    def poll(self):
        if self._on_poll is not None:
            callback, self._on_poll = self._on_poll, None
            callback()
        return self.return_code


class FakeBridge:
    def __init__(self, layout, platform, alive):
        self.layout = layout
        self.platform = platform
        self.alive = alive
        self.instance_id = "1" * 32
        self.calls = []
        self.shutdown_error = None

    def call(self, command, arguments=None):
        self.calls.append((command, arguments or {}))
        if command == "capabilities":
            return {
                "project_hash": self.layout.project_hash(self.platform),
                "bridge_version": unreal_editor_mcp.__version__,
                "bridge_instance_id": self.instance_id,
            }
        if command == "editor_shutdown":
            if self.shutdown_error is not None:
                raise self.shutdown_error
            self.alive.clear()
            return {"accepted": True}
        return {}

    def close(self):
        pass


class LifecycleTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.descriptor = self.root / "Space Project.uproject"
        self.descriptor.write_text("{}", encoding="utf-8")
        self.editor = self.root / "UnrealEditor.exe"
        self.editor.write_bytes(b"MZ")
        self.layout = ProjectLayout.resolve(self.descriptor)
        self.layout.state_dir.mkdir(parents=True)
        self.alive = set()
        self.platform = PlatformAdapter("windows", process_probe=lambda pid: pid in self.alive)
        self.bridge = FakeBridge(self.layout, self.platform, self.alive)

    def tearDown(self):
        self.temporary.cleanup()

    def _write_discovery(self, pid=10, *, updated_at_ms=None, version=None):
        self.alive.add(pid)
        value = {
            "project_hash": self.layout.project_hash(self.platform),
            "process_id": pid,
            "port": 15485,
            "bridge_version": version or unreal_editor_mcp.__version__,
            "unreal_version": "5.8.0",
            "updated_at_ms": updated_at_ms or __import__("time").time_ns() // 1_000_000,
        }
        self.layout.discovery_file.write_text(json.dumps(value), encoding="utf-8")

    def _manager(self, **kwargs):
        return EditorLifecycle(
            self.layout,
            self.bridge,
            editor_executable=self.editor,
            startup_timeout=5.0,
            platform=self.platform,
            **kwargs,
        )

    def test_large_mode_only_publishes_exact_lifecycle_schema(self):
        default_names = [tool["name"] for tool in MCPServer(self.bridge).tools]
        large = MCPServer(self.bridge, lifecycle=self._manager(), tool_mode="large")
        large_names = [tool["name"] for tool in large.tools]
        self.assertNotIn("editor_lifecycle", default_names)
        self.assertEqual(large_names[-1], "editor_lifecycle")
        valid = large.handle({
            "jsonrpc": "2.0", "id": 1, "method": "tools/call",
            "params": {"name": "editor_lifecycle", "arguments": {
                "operation_id": "a" * 32, "operation": "shutdown",
            }},
        })
        self.assertNotIn("error", valid)
        invalid = large.handle({
            "jsonrpc": "2.0", "id": 2, "method": "tools/call",
            "params": {"name": "editor_lifecycle", "arguments": {
                "operation_id": "b" * 32, "operation": "launch",
                "executable": "cmd.exe",
            }},
        })
        self.assertEqual(invalid["error"]["code"], -32602)

    def test_launch_uses_only_configured_paths_and_waits_for_exact_bridge(self):
        captured = {}

        def factory(command, **options):
            captured["command"] = command
            captured["options"] = options
            return FakeProcess(20, on_poll=lambda: self._publish_started_bridge(20, "2" * 32))

        result = self._manager(process_factory=factory).execute({
            "operation_id": "a" * 32,
            "operation": "launch",
        })
        self.assertEqual(result["state"], "ready")
        self.assertEqual(result["new_bridge_instance_id"], "2" * 32)
        self.assertEqual(
            captured["command"],
            (str(self.editor.resolve()), str(self.descriptor.resolve())),
        )
        self.assertFalse(captured["options"]["shell"])
        self.assertEqual(captured["options"]["creationflags"], 0x208)

    def _publish_started_bridge(self, pid, instance_id):
        self.bridge.instance_id = instance_id
        self._write_discovery(pid)

    def test_launch_is_idempotent_and_refuses_stale_live_process(self):
        self._write_discovery()
        first = self._manager().execute({"operation_id": "a" * 32, "operation": "launch"})
        self.assertEqual(first["state"], "already_running")
        self.layout.discovery_file.write_text(json.dumps({
            "project_hash": self.layout.project_hash(self.platform),
            "process_id": 10,
            "port": 15485,
            "bridge_version": unreal_editor_mcp.__version__,
            "unreal_version": "5.8.0",
            "updated_at_ms": 1,
        }), encoding="utf-8")
        with self.assertRaises(BridgeError) as caught:
            self._manager().execute({"operation_id": "b" * 32, "operation": "launch"})
        self.assertEqual(caught.exception.code, ErrorCode.BUSY)
        self._write_discovery(updated_at_ms=__import__("time").time_ns() // 1_000_000 + 60_000)
        with self.assertRaises(BridgeError) as caught:
            self._manager().execute({"operation_id": "c" * 32, "operation": "launch"})
        self.assertEqual(caught.exception.code, ErrorCode.BUSY)

    def test_shutdown_reconciles_process_exit_and_replays(self):
        self._write_discovery()
        manager = self._manager()
        arguments = {"operation_id": "a" * 32, "operation": "shutdown"}
        result = manager.execute(arguments)
        self.assertEqual(result["state"], "stopped")
        self.assertEqual(manager.execute(arguments), result)
        self.assertIn(("editor_shutdown", {}), self.bridge.calls)

    def test_shutdown_refusal_is_retained(self):
        self._write_discovery()
        self.bridge.shutdown_error = BridgeError(
            "Unsaved packages",
            code=ErrorCode.BUSY,
            details={"dirty_package_count": 1},
        )
        manager = self._manager()
        arguments = {"operation_id": "a" * 32, "operation": "shutdown"}
        for _ in range(2):
            with self.assertRaises(BridgeError) as caught:
                manager.execute(arguments)
            self.assertEqual(caught.exception.code, ErrorCode.BUSY)
        self.assertEqual(
            [call[0] for call in self.bridge.calls].count("editor_shutdown"),
            1,
        )

    def test_restart_records_old_and_new_bridge_instances(self):
        self._write_discovery()

        def factory(_command, **_options):
            return FakeProcess(20, on_poll=lambda: self._publish_started_bridge(20, "2" * 32))

        result = self._manager(process_factory=factory).execute({
            "operation_id": "a" * 32,
            "operation": "restart",
        })
        self.assertEqual(result["state"], "ready")
        self.assertEqual(result["old_bridge_instance_id"], "1" * 32)
        self.assertEqual(result["new_bridge_instance_id"], "2" * 32)

    def test_missing_launch_configuration_and_version_mismatch_fail_closed(self):
        manager = EditorLifecycle(
            self.layout,
            self.bridge,
            startup_timeout=5.0,
            platform=self.platform,
        )
        with self.assertRaises(ConfigurationError):
            manager.execute({"operation_id": "a" * 32, "operation": "launch"})
        self._write_discovery()
        with self.assertRaises(ConfigurationError):
            manager.execute({"operation_id": "b" * 32, "operation": "restart"})
        self.assertNotIn("editor_shutdown", [call[0] for call in self.bridge.calls])
        self.alive.clear()
        self.layout.discovery_file.unlink()
        self._write_discovery(version="99.0.0")
        with self.assertRaises(BridgeError) as caught:
            self._manager().execute({"operation_id": "c" * 32, "operation": "launch"})
        self.assertEqual(caught.exception.code, ErrorCode.VERSION_MISMATCH)

    def test_abnormal_startup_timeout_and_cancellation_are_bounded(self):
        with self.subTest("abnormal exit"):
            manager = self._manager(process_factory=lambda *_args, **_kwargs: FakeProcess(20, return_code=7))
            with self.assertRaises(BridgeError) as caught:
                manager.execute({"operation_id": "a" * 32, "operation": "launch"})
            self.assertEqual(caught.exception.code, ErrorCode.EDITOR_UNAVAILABLE)
            self.assertEqual(caught.exception.details["return_code"], 7)

        with self.subTest("timeout"):
            ticks = iter((0.0, 0.0, 6.0))
            manager = self._manager(
                process_factory=lambda *_args, **_kwargs: FakeProcess(21),
                monotonic=lambda: next(ticks),
            )
            with self.assertRaises(BridgeError) as caught:
                manager.execute({"operation_id": "b" * 32, "operation": "launch"})
            self.assertEqual(caught.exception.code, ErrorCode.TIMEOUT)
            self.assertTrue(caught.exception.details["editor_may_still_start"])

        with self.subTest("cancel wait without terminating child"):
            holder = {}

            def factory(*_args, **_kwargs):
                return FakeProcess(22, on_poll=holder["manager"].close)

            holder["manager"] = self._manager(process_factory=factory)
            with self.assertRaises(BridgeError) as caught:
                holder["manager"].execute({"operation_id": "c" * 32, "operation": "launch"})
            self.assertEqual(caught.exception.code, ErrorCode.CANCELLED)
            self.assertTrue(caught.exception.details["editor_may_still_start"])

    def test_shutdown_without_editor_is_idempotent(self):
        result = self._manager().execute({"operation_id": "a" * 32, "operation": "shutdown"})
        self.assertEqual(result["state"], "already_stopped")

    def test_interrupted_durable_record_becomes_outcome_unknown(self):
        now = __import__("time").time_ns() // 1_000_000
        record = {
            "operation_id": "a" * 32,
            "operation": "restart",
            "state": "shutting_down",
            "project_hash": self.layout.project_hash(self.platform),
            "python_version": unreal_editor_mcp.__version__,
            "old_bridge_instance_id": "1" * 32,
            "new_bridge_instance_id": "",
            "process_id": 10,
            "started_at_ms": now,
            "updated_at_ms": now,
            "error": None,
        }
        (self.layout.state_dir / "lifecycle.json").write_text(
            json.dumps({"version": 1, "records": [record]}),
            encoding="utf-8",
        )
        with self.assertRaises(BridgeError) as caught:
            self._manager().execute({"operation_id": "a" * 32, "operation": "restart"})
        self.assertEqual(caught.exception.code, ErrorCode.OUTCOME_UNKNOWN)

    def test_executable_validation_and_linux_command_are_fail_closed(self):
        self.assertEqual(
            resolve_editor_executable(self.editor, self.platform),
            self.editor.resolve(),
        )
        with self.assertRaises(ConfigurationError):
            resolve_editor_executable(Path("UnrealEditor.exe"), self.platform)
        linux = PlatformAdapter("linux", process_probe=lambda _pid: False)
        with self.assertRaises(ConfigurationError):
            linux.editor_launch_command(Path("/opt/UnrealEditor"), self.descriptor)


if __name__ == "__main__":
    unittest.main()
