from __future__ import annotations

import unittest

from godot_editor_mcp.errors import OperationTimeoutError
from godot_editor_mcp.waiting import OperationWaiter


class FakeClock:
    def __init__(self) -> None:
        self.value = 0.0

    def __call__(self) -> float:
        return self.value

    def sleep(self, seconds: float) -> None:
        self.value += seconds


class ScriptedBridge:
    def __init__(self, states: list[dict], asset: dict | None = None) -> None:
        self.states = states
        self.asset = asset or {"type": "Texture2D", "loadable": True}
        self.calls: list[tuple[str, dict]] = []

    def call(self, command: str, arguments: dict | None = None):
        self.calls.append((command, arguments or {}))
        if command == "state":
            if len(self.states) > 1:
                return self.states.pop(0)
            return self.states[0]
        if command == "asset_info":
            return self.asset
        raise AssertionError(command)


class OperationWaiterTests(unittest.TestCase):
    def waiter(self, bridge: ScriptedBridge) -> tuple[OperationWaiter, FakeClock]:
        clock = FakeClock()
        return OperationWaiter(bridge, clock=clock, sleep=clock.sleep), clock

    def test_scene_wait_uses_operation_and_diagnostic_quiet_period(self) -> None:
        bridge = ScriptedBridge([
            {
                "scene": "res://old.tscn",
                "active_operations": [{"operation_id": "op-1"}],
                "last_diagnostic_id": 3,
            },
            {
                "scene": "res://main.tscn",
                "active_operations": [],
                "last_diagnostic_id": 4,
            },
        ])
        waiter, clock = self.waiter(bridge)
        result = waiter.wait_for_scene("main.tscn", "op-1", 1000)
        self.assertEqual(result, {"completed": True, "scene": "res://main.tscn"})
        self.assertGreaterEqual(clock.value, 0.1)

    def test_import_failure_is_returned_as_bounded_completion(self) -> None:
        failure = {
            "path": "res://bad.png", "status": "failed",
            "error": {"message": "decode failed"},
        }
        bridge = ScriptedBridge([
            {
                "filesystem_scanning": False,
                "active_operations": [],
                "recent_imports": [failure],
                "last_diagnostic_id": 8,
            }
        ])
        waiter, _ = self.waiter(bridge)
        result = waiter.wait_for_asset("bad.png", "op-2", 1000)
        self.assertEqual(result["import"], failure)
        self.assertNotIn(("asset_info", {"path": "bad.png"}), bridge.calls)

    def test_run_reports_startup_health(self) -> None:
        bridge = ScriptedBridge([
            {
                "playing": True, "run_id": 7, "active_operations": [],
                "last_diagnostic_id": None, "last_run_exit_status": "running",
            }
        ])
        waiter, _ = self.waiter(bridge)
        result = waiter.wait_for_run(7, "op-3", 1000, 200)
        self.assertTrue(result["survived_startup_window"])
        self.assertEqual(result["run_id"], 7)

    def test_timeout_uses_stable_typed_error(self) -> None:
        bridge = ScriptedBridge([{
            "scene": "res://old.tscn", "active_operations": [],
            "last_diagnostic_id": None,
        }])
        waiter, _ = self.waiter(bridge)
        with self.assertRaises(OperationTimeoutError) as raised:
            waiter.wait_for_scene("never.tscn", "op-4", 100)
        self.assertEqual(raised.exception.code, "timeout")
        self.assertEqual(raised.exception.details["operation_id"], "op-4")

    def test_wait_options_are_strict_and_bridge_fields_are_removed(self) -> None:
        self.assertEqual(
            OperationWaiter.options({"wait": True, "timeout_ms": 25}), (True, 25)
        )
        self.assertEqual(
            OperationWaiter.bridge_arguments({
                "path": "x.tscn", "wait": True, "timeout_ms": 25,
                "startup_window_ms": 50,
            }),
            {"path": "x.tscn"},
        )
        with self.assertRaisesRegex(ValueError, "wait"):
            OperationWaiter.options({"wait": 1})


if __name__ == "__main__":
    unittest.main()
