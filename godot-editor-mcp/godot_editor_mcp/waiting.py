"""Bounded Python-side waits for asynchronous Godot editor operations."""

from __future__ import annotations

import time
from dataclasses import dataclass
from threading import Event
from typing import Any, Callable, Protocol

from . import __version__
from .errors import (
    BridgeError,
    ErrorCode,
    InvalidArgumentError,
    InvalidResponseError,
    OperationCancelledError,
    OperationTimeoutError,
    ProjectMismatchError,
    StaleOperationError,
    VersionMismatchError,
)
from .state_payloads import EditorStatePayload, ReloadStatusPayload


DEFAULT_TIMEOUT_MS = 10_000
MAX_TIMEOUT_MS = 120_000
DEFAULT_STARTUP_WINDOW_MS = 250
MAX_STARTUP_WINDOW_MS = 5_000
POLL_INTERVAL_SECONDS = 0.05
DIAGNOSTIC_QUIET_SECONDS = 0.10


class BridgeClient(Protocol):
    def call(self, command: str, arguments: dict[str, Any] | None = None) -> Any: ...


@dataclass(frozen=True)
class _Deadline:
    expires_at: float
    clock: Callable[[], float]

    @classmethod
    def after(
        cls, timeout_ms: int, clock: Callable[[], float]
    ) -> _Deadline:
        return cls(clock() + timeout_ms / 1000, clock)

    @property
    def expired(self) -> bool:
        return self.clock() >= self.expires_at

    @property
    def remaining(self) -> float:
        return max(0.0, self.expires_at - self.clock())


class OperationWaiter:
    """Poll concise bridge state without ever blocking Godot's main thread."""

    def __init__(
        self,
        bridge: BridgeClient,
        *,
        clock: Callable[[], float] = time.monotonic,
        sleep: Callable[[float], None] = time.sleep,
        cancelled: Event | None = None,
    ) -> None:
        self.bridge = bridge
        self.clock = clock
        self.sleep = sleep
        self.cancelled = cancelled or Event()

    def cancel(self) -> None:
        """Stop an in-flight wait when the owning MCP server shuts down."""
        self.cancelled.set()

    @staticmethod
    def options(arguments: dict[str, Any]) -> tuple[bool, int]:
        wait = arguments.get("wait", False)
        timeout_ms = arguments.get("timeout_ms", DEFAULT_TIMEOUT_MS)
        if not isinstance(wait, bool):
            raise InvalidArgumentError("wait must be a boolean")
        if type(timeout_ms) is not int or not 1 <= timeout_ms <= MAX_TIMEOUT_MS:
            raise InvalidArgumentError(f"timeout_ms must be between 1 and {MAX_TIMEOUT_MS}")
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
        state = self._until_state(
            timeout_ms,
            operation_id,
            lambda item: item.scene == expected
            and not item.operation_active(operation_id),
        )
        return {"completed": True, "scene": state.scene}

    def wait_for_asset(
        self, path: str, operation_id: Any, timeout_ms: int
    ) -> dict[str, Any]:
        expected = "res://" + path.removeprefix("res://")

        def complete(state: EditorStatePayload) -> bool:
            if state.filesystem_scanning:
                return False
            if state.operation_active(operation_id):
                return False
            if operation_id is None:
                return True
            return any(item.path == expected for item in state.recent_imports)

        state = self._until_state(timeout_ms, operation_id, complete)
        record = next(
            (
                item for item in state.recent_imports
                if item.path == expected
            ),
            None,
        )
        record_dict = None if record is None else record.as_dict()
        if record is not None and record.status == "failed":
            return {"completed": True, "import": record_dict}
        try:
            info = self.bridge.call("asset_info", {"path": path.removeprefix("res://")})
        except BridgeError:
            info = None
        return {"completed": True, "import": record_dict, "asset": info}

    def wait_for_run(
        self,
        run_id: int,
        operation_id: Any,
        timeout_ms: int,
        startup_window_ms: int,
    ) -> dict[str, Any]:
        if type(startup_window_ms) is not int or not 0 <= startup_window_ms <= MAX_STARTUP_WINDOW_MS:
            raise InvalidArgumentError(
                f"startup_window_ms must be between 0 and {MAX_STARTUP_WINDOW_MS}"
            )
        started = self._until_state(
            timeout_ms,
            operation_id,
            lambda item: item.playing and item.run_id == run_id,
        )
        deadline = _Deadline.after(startup_window_ms, self.clock)
        survived = True
        state = started
        while not deadline.expired:
            self._raise_if_cancelled(operation_id)
            self.sleep(min(POLL_INTERVAL_SECONDS, deadline.remaining))
            state = self._state()
            if not state.playing or state.run_id != run_id:
                survived = False
                break
        return {
            "completed": True,
            "run_id": run_id,
            "startup_window_ms": startup_window_ms,
            "survived_startup_window": survived,
            "last_run_exit_status": state.last_run_exit_status,
        }

    def wait_for_stop(
        self, run_id: int, operation_id: Any, timeout_ms: int
    ) -> dict[str, Any]:
        state = self._until_state(
            timeout_ms,
            operation_id,
            lambda item: not item.playing and item.last_run_id == run_id,
        )
        return {
            "completed": True,
            "run_id": run_id,
            "last_run_exit_status": state.last_run_exit_status,
            "last_stop_reason": state.last_stop_reason,
        }

    def wait_for_reload(
        self,
        operation_id: Any,
        expected_project_hash: Any,
        expected_bridge_version: Any,
        timeout_ms: int,
    ) -> dict[str, Any]:
        if not isinstance(operation_id, str) or not operation_id:
            raise InvalidResponseError("Godot editor did not return a reload operation ID")
        if (
            not isinstance(expected_project_hash, str)
            or len(expected_project_hash) != 64
            or any(
                character not in "0123456789abcdef"
                for character in expected_project_hash
            )
        ):
            raise InvalidResponseError("Godot editor did not return a valid project hash")
        if not isinstance(expected_bridge_version, str) or not expected_bridge_version:
            raise InvalidResponseError("Godot editor did not return a bridge version")
        if expected_bridge_version != __version__:
            raise VersionMismatchError(
                "Python server and Godot plugin versions do not match",
                details={
                    "mcp_server_version": __version__,
                    "bridge_version": expected_bridge_version,
                },
            )

        deadline = _Deadline.after(timeout_ms, self.clock)
        disconnected = False
        while True:
            self._raise_if_cancelled(operation_id)
            try:
                status = self.bridge.call(
                    "reload_status", {"operation_id": operation_id}
                )
            except BridgeError as exc:
                if exc.code not in {
                    ErrorCode.EDITOR_UNAVAILABLE,
                    ErrorCode.UNAUTHORIZED,
                }:
                    raise
                disconnected = True
                status = None
            if status is not None:
                view = ReloadStatusPayload.from_payload(status)
                if view.operation_id != operation_id:
                    raise StaleOperationError(
                        "Reload operation ID changed during reconnect",
                        details={
                            "expected_operation_id": operation_id,
                            "operation_id": view.operation_id,
                        },
                    )
                if view.project_hash != expected_project_hash:
                    raise ProjectMismatchError(
                        "Godot bridge reconnected to another project",
                        details={
                            "expected_project_hash": expected_project_hash,
                            "project_hash": view.project_hash,
                        },
                    )
                if view.bridge_version != expected_bridge_version:
                    raise VersionMismatchError(
                        "Bridge version changed during project reload",
                        details={
                            "expected_bridge_version": expected_bridge_version,
                            "bridge_version": view.bridge_version,
                        },
                    )
                if view.completed:
                    return {**view.as_dict(), "disconnected": disconnected}
            if deadline.expired:
                raise OperationTimeoutError(
                    "Godot project reload timed out",
                    code=ErrorCode.TIMEOUT,
                    details={"operation_id": operation_id, "timeout_ms": timeout_ms},
                    retryable=True,
                )
            self.sleep(POLL_INTERVAL_SECONDS)

    def _until_state(
        self,
        timeout_ms: int,
        operation_id: Any,
        predicate: Callable[[EditorStatePayload], bool],
    ) -> EditorStatePayload:
        deadline = _Deadline.after(timeout_ms, self.clock)
        unset = object()
        last_diagnostic_id: Any = unset
        quiet_since: float | None = None
        while True:
            self._raise_if_cancelled(operation_id)
            state = self._state()
            if predicate(state):
                diagnostic_id = state.last_diagnostic_id
                now = self.clock()
                if diagnostic_id != last_diagnostic_id:
                    last_diagnostic_id = diagnostic_id
                    quiet_since = now
                elif quiet_since is not None and now - quiet_since >= DIAGNOSTIC_QUIET_SECONDS:
                    return state
            else:
                quiet_since = None
                last_diagnostic_id = unset
            if deadline.expired:
                raise OperationTimeoutError(
                    "Godot editor operation timed out",
                    code=ErrorCode.TIMEOUT,
                    details={"operation_id": operation_id, "timeout_ms": timeout_ms},
                    retryable=True,
                )
            self.sleep(POLL_INTERVAL_SECONDS)

    def _raise_if_cancelled(self, operation_id: Any) -> None:
        if self.cancelled.is_set():
            raise OperationCancelledError(
                "Godot editor operation wait was cancelled",
                details={"operation_id": operation_id},
            )

    def _state(self) -> EditorStatePayload:
        state = EditorStatePayload.from_payload(self.bridge.call("state", {}))
        if state.bridge_version != __version__:
            raise VersionMismatchError(
                "Python server and Godot plugin versions do not match",
                details={
                    "mcp_server_version": __version__,
                    "bridge_version": state.bridge_version,
                },
            )
        return state


__all__ = [
    "DEFAULT_STARTUP_WINDOW_MS",
    "DEFAULT_TIMEOUT_MS",
    "MAX_STARTUP_WINDOW_MS",
    "MAX_TIMEOUT_MS",
    "OperationWaiter",
]
