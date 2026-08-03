"""Configured, durable, and bounded Unreal Editor lifecycle control."""

from __future__ import annotations

import json
import os
import secrets
import threading
import time
from collections.abc import Callable
from pathlib import Path
from typing import Any, Protocol

from . import __version__
from .discovery import DiscoveryRecord, read_discovery, read_discovery_record
from .errors import BridgeError, ConfigurationError, DomainError, ErrorCode
from .platforms import DEFAULT_PLATFORM, PlatformAdapter
from .project import ProjectLayout


MAX_LIFECYCLE_RECORDS = 16
MAX_LIFECYCLE_BYTES = 32 * 1024
MAX_LIFECYCLE_AGE_MS = 24 * 60 * 60 * 1000
MIN_LIFECYCLE_TIMEOUT = 5.0
MAX_LIFECYCLE_TIMEOUT = 900.0
POLL_SECONDS = 0.25
TERMINAL_STATES = {
    "ready", "already_running", "stopped", "already_stopped", "cancelled",
    "timed_out", "failed", "rejected", "outcome_unknown",
}


class BridgeClient(Protocol):
    def call(self, command: str, arguments: dict[str, Any] | None = None) -> Any: ...


def resolve_editor_executable(value: str | Path, platform: PlatformAdapter = DEFAULT_PLATFORM) -> Path:
    candidate = Path(value)
    if not candidate.is_absolute():
        raise ConfigurationError("Editor executable must be an absolute path")
    try:
        resolved = candidate.resolve(strict=True)
    except (OSError, RuntimeError):
        raise ConfigurationError("Editor executable must be an existing regular file") from None
    if not resolved.is_file():
        raise ConfigurationError("Editor executable must be an existing regular file")
    if platform.system == "windows" and resolved.name.casefold() != "unrealeditor.exe":
        raise ConfigurationError("Windows editor executable must be UnrealEditor.exe")
    if platform.system == "macos" and resolved.name != "UnrealEditor":
        raise ConfigurationError("macOS editor executable must be the UnrealEditor app binary")
    if platform.system == "linux":
        raise ConfigurationError("Configured editor launch is supported only on macOS and Windows")
    if platform.system != "windows" and not os.access(resolved, os.X_OK):
        raise ConfigurationError("Editor executable is not executable")
    return resolved


class EditorLifecycle:
    def __init__(
        self,
        layout: ProjectLayout,
        bridge: BridgeClient,
        *,
        editor_executable: Path,
        startup_timeout: float = 120.0,
        platform: PlatformAdapter = DEFAULT_PLATFORM,
        process_factory: Callable[..., Any] | None = None,
        monotonic: Callable[[], float] = time.monotonic,
        now_ms: Callable[[], int] = lambda: time.time_ns() // 1_000_000,
    ) -> None:
        if not MIN_LIFECYCLE_TIMEOUT <= startup_timeout <= MAX_LIFECYCLE_TIMEOUT:
            raise ConfigurationError("Lifecycle timeout must be between 5 and 900 seconds")
        self.layout = layout
        self.bridge = bridge
        self.platform = platform
        self.editor_executable = resolve_editor_executable(editor_executable, platform)
        self.startup_timeout = float(startup_timeout)
        self._process_factory = process_factory
        self._monotonic = monotonic
        self._now_ms = now_ms
        self._operation_lock = threading.Lock()
        self._state_lock = threading.RLock()
        self._cancel: dict[str, threading.Event] = {}
        self._closed = False
        self._records = self._load_records()
        self._mark_interrupted_records()

    @property
    def record_file(self) -> Path:
        return self.layout.state_dir / "lifecycle.json"

    def availability(self) -> dict[str, Any]:
        return {
            "enabled": True,
            "platform": self.platform.system,
            "launch_configured": True,
            "launch_supported": self.platform.system in {"macos", "windows"},
            "operations": ["launch", "shutdown", "restart", "cancel"],
            "startup_timeout_ms": int(self.startup_timeout * 1000),
            "retained_records": MAX_LIFECYCLE_RECORDS,
            "record_lifetime_ms": MAX_LIFECYCLE_AGE_MS,
        }

    def execute(self, arguments: dict[str, Any]) -> dict[str, Any]:
        operation = arguments["operation"]
        operation_id = arguments["operation_id"]
        if operation == "cancel":
            return self._cancel_operation(operation_id)
        with self._state_lock:
            replay = self._find_record(operation_id)
            if replay is not None:
                if replay["operation"] != operation:
                    raise BridgeError(
                        "Lifecycle operation ID is already bound to another request",
                        code=ErrorCode.OPERATION_CONFLICT,
                    )
                return self._replay(replay)
        if not self._operation_lock.acquire(blocking=False):
            raise BridgeError("Another editor lifecycle operation is active", code=ErrorCode.BUSY, retryable=True)
        event = threading.Event()
        try:
            with self._state_lock:
                if self._closed:
                    raise BridgeError("Lifecycle controller is closed", code=ErrorCode.CANCELLED)
                self._cancel[operation_id] = event
                record = self._new_record(operation_id, operation)
            try:
                if operation == "launch":
                    result = self._launch(record, event)
                elif operation == "shutdown":
                    result = self._shutdown(record, event)
                else:
                    result = self._restart(record, event)
                return result
            except DomainError as exc:
                state = {
                    ErrorCode.CANCELLED: "cancelled",
                    ErrorCode.TIMEOUT: "timed_out",
                    ErrorCode.OUTCOME_UNKNOWN: "outcome_unknown",
                    ErrorCode.BUSY: "rejected",
                }.get(exc.code, "failed")
                self._update(record, state=state, error=exc.as_dict())
                raise
        finally:
            with self._state_lock:
                self._cancel.pop(operation_id, None)
            self._operation_lock.release()

    def close(self) -> None:
        with self._state_lock:
            self._closed = True
            events = list(self._cancel.values())
        for event in events:
            event.set()

    def _launch(self, record: dict[str, Any], event: threading.Event) -> dict[str, Any]:
        active = self._active_discovery()
        if active is not None:
            capabilities = self._verify_bridge(active)
            return self._finish(
                record,
                "already_running",
                process_id=active.process_id,
                new_bridge_instance_id=capabilities["bridge_instance_id"],
            )
        self._refuse_live_stale_record()
        self._check_cancel(event, editor_may_still_start=False)
        try:
            if self._process_factory is None:
                process = self.platform.launch_editor(self.editor_executable, self.layout.descriptor)
            else:
                process = self.platform.launch_editor(
                    self.editor_executable,
                    self.layout.descriptor,
                    process_factory=self._process_factory,
                )
        except DomainError:
            raise
        except OSError as exc:
            raise BridgeError(
                "Configured Unreal Editor could not be started",
                code=ErrorCode.EDITOR_UNAVAILABLE,
                details={"os_error": str(exc)[:128]},
                retryable=True,
            ) from None
        self._update(record, state="starting", process_id=int(process.pid))
        deadline = self._monotonic() + self.startup_timeout
        while self._monotonic() < deadline:
            self._check_cancel(event, editor_may_still_start=True)
            return_code = process.poll()
            if return_code is not None:
                raise BridgeError(
                    "Configured Unreal Editor exited before its bridge became ready",
                    code=ErrorCode.EDITOR_UNAVAILABLE,
                    details={"return_code": int(return_code)},
                    retryable=True,
                )
            active = self._active_discovery()
            if active is not None:
                if active.process_id != process.pid:
                    raise BridgeError(
                        "A different process published the configured project bridge",
                        code=ErrorCode.INVALID_RESPONSE,
                    )
                try:
                    capabilities = self._verify_bridge(active)
                except BridgeError as exc:
                    if exc.code not in {ErrorCode.TIMEOUT, ErrorCode.EDITOR_UNAVAILABLE}:
                        raise
                    event.wait(POLL_SECONDS)
                    continue
                return self._finish(
                    record,
                    "ready",
                    process_id=active.process_id,
                    new_bridge_instance_id=capabilities["bridge_instance_id"],
                )
            event.wait(POLL_SECONDS)
        raise BridgeError(
            "Timed out waiting for the configured Unreal Editor bridge",
            code=ErrorCode.TIMEOUT,
            details={"editor_may_still_start": True},
            retryable=True,
        )

    def _shutdown(self, record: dict[str, Any], event: threading.Event) -> dict[str, Any]:
        active = self._active_discovery()
        if active is None:
            self._refuse_live_stale_record()
            return self._finish(record, "already_stopped")
        capabilities = self._verify_bridge(active)
        old_instance = capabilities["bridge_instance_id"]
        self._update(
            record,
            state="shutdown_preflight",
            process_id=active.process_id,
            old_bridge_instance_id=old_instance,
        )
        self._check_cancel(event, editor_may_still_start=False)
        try:
            accepted = self.bridge.call("editor_shutdown", {})
        except BridgeError:
            if not self.platform.process_is_alive(active.process_id):
                return self._finish(record, "stopped")
            raise
        if not isinstance(accepted, dict) or accepted.get("accepted") is not True:
            raise BridgeError("Unreal Editor returned an invalid shutdown acceptance", code=ErrorCode.INVALID_RESPONSE)
        self._update(record, state="shutting_down")
        deadline = self._monotonic() + self.startup_timeout
        while self._monotonic() < deadline:
            if not self.platform.process_is_alive(active.process_id):
                return self._finish(record, "stopped")
            event.wait(POLL_SECONDS)
        raise BridgeError(
            "Graceful editor shutdown was accepted but did not finish before the timeout",
            code=ErrorCode.OUTCOME_UNKNOWN,
            details={"process_id": active.process_id},
        )

    def _restart(self, record: dict[str, Any], event: threading.Event) -> dict[str, Any]:
        active = self._active_discovery()
        old_instance = ""
        if active is None:
            self._refuse_live_stale_record()
        if active is not None:
            capabilities = self._verify_bridge(active)
            old_instance = capabilities["bridge_instance_id"]
            self._update(
                record,
                state="shutdown_preflight",
                process_id=active.process_id,
                old_bridge_instance_id=old_instance,
            )
            self._check_cancel(event, editor_may_still_start=False)
            accepted = self.bridge.call("editor_shutdown", {})
            if not isinstance(accepted, dict) or accepted.get("accepted") is not True:
                raise BridgeError("Unreal Editor returned an invalid shutdown acceptance", code=ErrorCode.INVALID_RESPONSE)
            self._update(record, state="shutting_down")
            deadline = self._monotonic() + self.startup_timeout
            while self._monotonic() < deadline and self.platform.process_is_alive(active.process_id):
                event.wait(POLL_SECONDS)
            if self.platform.process_is_alive(active.process_id):
                raise BridgeError(
                    "Restart shutdown did not finish before the timeout",
                    code=ErrorCode.OUTCOME_UNKNOWN,
                    details={"process_id": active.process_id},
                )
        self._check_cancel(event, editor_may_still_start=False)
        self._update(record, state="launching")
        result = self._launch(record, event)
        new_instance = result.get("new_bridge_instance_id", "")
        if old_instance and new_instance == old_instance:
            raise BridgeError("Restart rediscovered the previous bridge instance", code=ErrorCode.INVALID_RESPONSE)
        record["old_bridge_instance_id"] = old_instance
        self._persist_records()
        result["old_bridge_instance_id"] = old_instance
        return result

    def _active_discovery(self) -> DiscoveryRecord | None:
        try:
            return read_discovery(self.layout, platform=self.platform)
        except BridgeError as exc:
            if exc.code == ErrorCode.EDITOR_UNAVAILABLE:
                return None
            raise

    def _refuse_live_stale_record(self) -> None:
        try:
            read_discovery_record(self.layout, platform=self.platform)
        except BridgeError:
            return
        raise BridgeError(
            "A configured-project editor process is alive but its bridge heartbeat is stale",
            code=ErrorCode.BUSY,
            retryable=True,
        )

    def _verify_bridge(self, record: DiscoveryRecord) -> dict[str, Any]:
        expected_hash = self.layout.project_hash(self.platform)
        if record.project_hash != expected_hash:
            raise BridgeError("Discovered bridge does not match the configured project", code=ErrorCode.INVALID_RESPONSE)
        if record.bridge_version != __version__:
            raise BridgeError(
                "Python server and Unreal plugin versions do not match",
                code=ErrorCode.VERSION_MISMATCH,
                details={"python_version": __version__, "bridge_version": record.bridge_version},
            )
        capabilities = self.bridge.call("capabilities", {})
        if (
            not isinstance(capabilities, dict)
            or capabilities.get("project_hash") != expected_hash
            or capabilities.get("bridge_version") != __version__
            or not isinstance(capabilities.get("bridge_instance_id"), str)
            or len(capabilities["bridge_instance_id"]) != 32
        ):
            raise BridgeError("Authenticated bridge identity is invalid", code=ErrorCode.INVALID_RESPONSE)
        return capabilities

    def _check_cancel(self, event: threading.Event, *, editor_may_still_start: bool) -> None:
        if event.is_set():
            raise BridgeError(
                "Editor lifecycle wait was cancelled",
                code=ErrorCode.CANCELLED,
                details={"editor_may_still_start": editor_may_still_start},
            )

    def _cancel_operation(self, operation_id: str) -> dict[str, Any]:
        with self._state_lock:
            event = self._cancel.get(operation_id)
            record = self._find_record(operation_id)
            if event is not None:
                event.set()
                return {
                    "operation_id": operation_id,
                    "operation": record["operation"] if record else "",
                    "state": "cancellation_requested",
                }
            if record is not None:
                return self._public_record(record)
        raise BridgeError("Lifecycle operation was not found", code=ErrorCode.NOT_FOUND)

    def _new_record(self, operation_id: str, operation: str) -> dict[str, Any]:
        now = self._now_ms()
        record = {
            "operation_id": operation_id,
            "operation": operation,
            "state": "accepted",
            "project_hash": self.layout.project_hash(self.platform),
            "python_version": __version__,
            "old_bridge_instance_id": "",
            "new_bridge_instance_id": "",
            "process_id": 0,
            "started_at_ms": now,
            "updated_at_ms": now,
            "error": None,
        }
        self._records.append(record)
        self._trim_records()
        self._persist_records()
        return record

    def _update(self, record: dict[str, Any], **changes: Any) -> None:
        with self._state_lock:
            record.update(changes)
            record["updated_at_ms"] = self._now_ms()
            self._persist_records()

    def _finish(self, record: dict[str, Any], state: str, **changes: Any) -> dict[str, Any]:
        self._update(record, state=state, error=None, **changes)
        return self._public_record(record)

    def _replay(self, record: dict[str, Any]) -> dict[str, Any]:
        error = record.get("error")
        if isinstance(error, dict):
            try:
                code = ErrorCode(error.get("code"))
            except (TypeError, ValueError):
                code = ErrorCode.INTERNAL_ERROR
            raise BridgeError(
                str(error.get("message", "Retained lifecycle operation failed")),
                code=code,
                details=error.get("details") if isinstance(error.get("details"), dict) else None,
                retryable=error.get("retryable") is True,
            )
        return self._public_record(record)

    @staticmethod
    def _public_record(record: dict[str, Any]) -> dict[str, Any]:
        return {
            key: record[key]
            for key in (
                "operation_id", "operation", "state", "project_hash", "python_version",
                "old_bridge_instance_id", "new_bridge_instance_id", "process_id",
                "started_at_ms", "updated_at_ms",
            )
        }

    def _find_record(self, operation_id: str) -> dict[str, Any] | None:
        return next((item for item in reversed(self._records) if item.get("operation_id") == operation_id), None)

    def _load_records(self) -> list[dict[str, Any]]:
        path = self.record_file
        if not path.exists():
            return []
        if path.is_symlink():
            raise ConfigurationError("Lifecycle record must not be a symbolic link")
        try:
            if path.stat().st_size > MAX_LIFECYCLE_BYTES:
                return []
            root = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError):
            return []
        if not isinstance(root, dict) or set(root) != {"version", "records"} or root["version"] != 1:
            return []
        if not isinstance(root["records"], list) or len(root["records"]) > MAX_LIFECYCLE_RECORDS:
            return []
        now = self._now_ms()
        records = []
        for item in root["records"]:
            if (
                self._valid_record(item)
                and item["project_hash"] == self.layout.project_hash(self.platform)
                and 0 <= now - item["updated_at_ms"] <= MAX_LIFECYCLE_AGE_MS
            ):
                records.append(item)
        return records

    @staticmethod
    def _valid_record(item: object) -> bool:
        if not isinstance(item, dict) or set(item) != {
            "operation_id", "operation", "state", "project_hash", "python_version",
            "old_bridge_instance_id", "new_bridge_instance_id", "process_id",
            "started_at_ms", "updated_at_ms", "error",
        }:
            return False
        error = item["error"]
        valid_error = error is None or (
            isinstance(error, dict)
            and set(error) == {"code", "message", "details", "retryable"}
            and isinstance(error["code"], str)
            and isinstance(error["message"], str)
            and len(error["message"]) <= 512
            and isinstance(error["details"], dict)
            and len(error["details"]) <= 16
            and type(error["retryable"]) is bool
        )
        return (
            isinstance(item["operation_id"], str)
            and len(item["operation_id"]) == 32
            and all(character in "0123456789abcdef" for character in item["operation_id"])
            and item["operation"] in {"launch", "shutdown", "restart"}
            and isinstance(item["state"], str)
            and len(item["state"]) <= 32
            and isinstance(item["project_hash"], str)
            and len(item["project_hash"]) == 40
            and isinstance(item["python_version"], str)
            and len(item["python_version"]) <= 32
            and all(
                isinstance(item[key], str) and len(item[key]) in {0, 32}
                for key in ("old_bridge_instance_id", "new_bridge_instance_id")
            )
            and type(item["process_id"]) is int
            and item["process_id"] >= 0
            and type(item["started_at_ms"]) is int
            and type(item["updated_at_ms"]) is int
            and valid_error
        )

    def _mark_interrupted_records(self) -> None:
        changed = False
        for record in self._records:
            if record.get("state") not in TERMINAL_STATES:
                record["state"] = "outcome_unknown"
                record["updated_at_ms"] = self._now_ms()
                record["error"] = {
                    "code": ErrorCode.OUTCOME_UNKNOWN.value,
                    "message": "Lifecycle controller stopped before the durable operation reached a terminal state",
                    "details": {},
                    "retryable": False,
                }
                changed = True
        if changed:
            self._persist_records()

    def _trim_records(self) -> None:
        cutoff = self._now_ms() - MAX_LIFECYCLE_AGE_MS
        self._records[:] = [item for item in self._records if item.get("updated_at_ms", 0) >= cutoff]
        self._records[:] = self._records[-MAX_LIFECYCLE_RECORDS:]

    def _persist_records(self) -> None:
        self.layout.state_dir.mkdir(parents=True, exist_ok=True)
        if self.record_file.is_symlink():
            raise ConfigurationError("Lifecycle record must not be a symbolic link")
        payload = json.dumps(
            {"version": 1, "records": self._records[-MAX_LIFECYCLE_RECORDS:]},
            ensure_ascii=True,
            separators=(",", ":"),
            sort_keys=True,
        ).encode("utf-8")
        if len(payload) > MAX_LIFECYCLE_BYTES:
            raise BridgeError("Lifecycle record exceeded its size limit", code=ErrorCode.INTERNAL_ERROR)
        temporary = self.record_file.with_name(
            f".{self.record_file.name}.{os.getpid()}.{secrets.token_hex(8)}.tmp"
        )
        try:
            with temporary.open("wb") as stream:
                stream.write(payload)
                stream.flush()
                os.fsync(stream.fileno())
            if os.name != "nt":
                os.chmod(temporary, 0o600)
            os.replace(temporary, self.record_file)
        finally:
            try:
                temporary.unlink()
            except FileNotFoundError:
                pass
