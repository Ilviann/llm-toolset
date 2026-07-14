"""Bounded Python-side waits for asynchronous Godot editor operations."""

from __future__ import annotations

import time
from typing import Any, Callable, Protocol

from .errors import ErrorCode, OperationTimeoutError


DEFAULT_TIMEOUT_MS = 10_000
MAX_TIMEOUT_MS = 120_000
DEFAULT_STARTUP_WINDOW_MS = 250
MAX_STARTUP_WINDOW_MS = 5_000
POLL_INTERVAL_SECONDS = 0.05
DIAGNOSTIC_QUIET_SECONDS = 0.10


class BridgeClient(Protocol):
    def call(self, command: str, arguments: dict[str, Any] | None = None) -> Any: ...


class OperationWaiter:
    """Poll concise bridge state without ever blocking Godot's main thread."""

    def __init__(
        self,
        bridge: BridgeClient,
        *,
        clock: Callable[[], float] = time.monotonic,
        sleep: Callable[[float], None] = time.sleep,
    ) -> None:
        self.bridge = bridge
        self.clock = clock
        self.sleep = sleep

    @staticmethod
    def options(arguments: dict[str, Any]) -> tuple[bool, int]:
        wait = arguments.get("wait", False)
        timeout_ms = arguments.get("timeout_ms", DEFAULT_TIMEOUT_MS)
        if not isinstance(wait, bool):
            raise ValueError("wait must be a boolean")
        if type(timeout_ms) is not int or not 1 <= timeout_ms <= MAX_TIMEOUT_MS:
            raise ValueError(f"timeout_ms must be between 1 and {MAX_TIMEOUT_MS}")
        return wait, timeout_ms

    @staticmethod
    def bridge_arguments(arguments: dict[str, Any]) -> dict[str, Any]:
        return {
            key: value for key, value in arguments.items()
            if key not in {"wait", "timeout_ms", "startup_window_ms"}
        }

    def wait_for_scene(
        self, path: str, operation_id: Any, timeout_ms: int
    ) -> dict[str, Any]:
        expected = "res://" + path.removeprefix("res://")
        state = self._until(
            timeout_ms,
            operation_id,
            lambda item: item.get("scene") == expected
            and not self._operation_active(item, operation_id),
        )
        return {"completed": True, "scene": state.get("scene")}

    def wait_for_asset(
        self, path: str, operation_id: Any, timeout_ms: int
    ) -> dict[str, Any]:
        expected = "res://" + path.removeprefix("res://")

        def complete(state: dict[str, Any]) -> bool:
            if state.get("filesystem_scanning"):
                return False
            if self._operation_active(state, operation_id):
                return False
            for item in state.get("recent_imports", []):
                if isinstance(item, dict) and item.get("path") == expected:
                    if item.get("status") == "failed":
                        return True
                    if item.get("status") == "completed":
                        return True
            # Older plugins do not expose per-resource imports. Operation
            # completion remains a safe transitional completion signal.
            return True

        state = self._until(timeout_ms, operation_id, complete)
        record = next(
            (
                item for item in state.get("recent_imports", [])
                if isinstance(item, dict) and item.get("path") == expected
            ),
            None,
        )
        if isinstance(record, dict) and record.get("status") == "failed":
            return {"completed": True, "import": record}
        try:
            info = self.bridge.call("asset_info", {"path": path.removeprefix("res://")})
        except Exception:
            info = None
        return {"completed": True, "import": record, "asset": info}

    def wait_for_run(
        self,
        run_id: int,
        operation_id: Any,
        timeout_ms: int,
        startup_window_ms: int,
    ) -> dict[str, Any]:
        if type(startup_window_ms) is not int or not 0 <= startup_window_ms <= MAX_STARTUP_WINDOW_MS:
            raise ValueError(
                f"startup_window_ms must be between 0 and {MAX_STARTUP_WINDOW_MS}"
            )
        started = self._until(
            timeout_ms,
            operation_id,
            lambda item: item.get("playing") is True and item.get("run_id") == run_id,
        )
        deadline = self.clock() + startup_window_ms / 1000
        survived = True
        state = started
        while self.clock() < deadline:
            self.sleep(min(POLL_INTERVAL_SECONDS, max(0.0, deadline - self.clock())))
            state = self._state()
            if state.get("playing") is not True or state.get("run_id") != run_id:
                survived = False
                break
        return {
            "completed": True,
            "run_id": run_id,
            "startup_window_ms": startup_window_ms,
            "survived_startup_window": survived,
            "last_run_exit_status": state.get("last_run_exit_status"),
        }

    def wait_for_stop(
        self, run_id: int, operation_id: Any, timeout_ms: int
    ) -> dict[str, Any]:
        state = self._until(
            timeout_ms,
            operation_id,
            lambda item: item.get("playing") is False
            and item.get("last_run_id") == run_id,
        )
        return {
            "completed": True,
            "run_id": run_id,
            "last_run_exit_status": state.get("last_run_exit_status"),
            "last_stop_reason": state.get("last_stop_reason"),
        }

    def _until(
        self,
        timeout_ms: int,
        operation_id: Any,
        predicate: Callable[[dict[str, Any]], bool],
    ) -> dict[str, Any]:
        deadline = self.clock() + timeout_ms / 1000
        unset = object()
        last_diagnostic_id: Any = unset
        quiet_since: float | None = None
        while True:
            state = self._state()
            if predicate(state):
                diagnostic_id = state.get("last_diagnostic_id")
                now = self.clock()
                if diagnostic_id != last_diagnostic_id:
                    last_diagnostic_id = diagnostic_id
                    quiet_since = now
                elif quiet_since is not None and now - quiet_since >= DIAGNOSTIC_QUIET_SECONDS:
                    return state
            else:
                quiet_since = None
                last_diagnostic_id = unset
            if self.clock() >= deadline:
                raise OperationTimeoutError(
                    "Godot editor operation timed out",
                    code=ErrorCode.TIMEOUT,
                    details={"operation_id": operation_id, "timeout_ms": timeout_ms},
                    retryable=True,
                )
            self.sleep(POLL_INTERVAL_SECONDS)

    def _state(self) -> dict[str, Any]:
        state = self.bridge.call("state", {})
        if not isinstance(state, dict):
            raise ValueError("Godot editor returned invalid state while waiting")
        return state

    @staticmethod
    def _operation_active(state: dict[str, Any], operation_id: Any) -> bool:
        if operation_id is None:
            return False
        return any(
            isinstance(item, dict) and item.get("operation_id") == operation_id
            for item in state.get("active_operations", [])
        )


__all__ = [
    "DEFAULT_STARTUP_WINDOW_MS",
    "DEFAULT_TIMEOUT_MS",
    "MAX_STARTUP_WINDOW_MS",
    "MAX_TIMEOUT_MS",
    "OperationWaiter",
]
