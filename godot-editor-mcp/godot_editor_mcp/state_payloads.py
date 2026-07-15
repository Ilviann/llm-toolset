"""Validated views over the concise editor-state and reload wire payloads."""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass
from typing import Any

from .errors import InvalidResponseError


def _object(payload: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(payload, Mapping):
        raise InvalidResponseError(f"Godot editor returned invalid {label}")
    return payload


def _string(payload: Mapping[str, Any], field: str, *, empty: bool = True) -> str:
    value = payload.get(field)
    if not isinstance(value, str) or (not empty and not value):
        raise InvalidResponseError(f"Godot editor state has invalid {field}")
    return value


def _boolean(payload: Mapping[str, Any], field: str) -> bool:
    value = payload.get(field)
    if not isinstance(value, bool):
        raise InvalidResponseError(f"Godot editor state has invalid {field}")
    return value


def _optional_int(payload: Mapping[str, Any], field: str) -> int | None:
    value = payload.get(field)
    if value is not None and type(value) is not int:
        raise InvalidResponseError(f"Godot editor state has invalid {field}")
    return value


@dataclass(frozen=True)
class ImportPayload:
    """One validated recent-import record."""

    raw: Mapping[str, Any]

    @classmethod
    def from_payload(cls, payload: Any) -> ImportPayload:
        raw = _object(payload, "recent import record")
        _string(raw, "operation_id", empty=False)
        _string(raw, "path", empty=False)
        status = _string(raw, "status", empty=False)
        if status not in {"completed", "failed"}:
            raise InvalidResponseError("Godot editor state has invalid import status")
        return cls(raw)

    @property
    def path(self) -> str:
        return str(self.raw["path"])

    @property
    def status(self) -> str:
        return str(self.raw["status"])

    def as_dict(self) -> dict[str, Any]:
        return dict(self.raw)


@dataclass(frozen=True)
class EditorStatePayload:
    """Field-validating view used by wait completion predicates."""

    raw: Mapping[str, Any]

    @classmethod
    def from_payload(cls, payload: Any) -> EditorStatePayload:
        return cls(_object(payload, "state while waiting"))

    @property
    def scene(self) -> str:
        return _string(self.raw, "scene")

    @property
    def bridge_version(self) -> str:
        return _string(self.raw, "bridge_version", empty=False)

    @property
    def playing(self) -> bool:
        return _boolean(self.raw, "playing")

    @property
    def filesystem_scanning(self) -> bool:
        return _boolean(self.raw, "filesystem_scanning")

    @property
    def run_id(self) -> int | None:
        return _optional_int(self.raw, "run_id")

    @property
    def last_run_id(self) -> int | None:
        return _optional_int(self.raw, "last_run_id")

    @property
    def last_run_exit_status(self) -> str:
        return _string(self.raw, "last_run_exit_status", empty=False)

    @property
    def last_stop_reason(self) -> str:
        return _string(self.raw, "last_stop_reason")

    @property
    def last_diagnostic_id(self) -> int | None:
        return _optional_int(self.raw, "last_diagnostic_id")

    @property
    def recent_imports(self) -> tuple[ImportPayload, ...]:
        records = self.raw.get("recent_imports")
        if not isinstance(records, list):
            raise InvalidResponseError("Godot editor state has invalid recent_imports")
        return tuple(ImportPayload.from_payload(record) for record in records)

    def operation_active(self, operation_id: Any) -> bool:
        if operation_id is None:
            return False
        operations = self.raw.get("active_operations")
        if not isinstance(operations, list):
            raise InvalidResponseError("Godot editor state has invalid active_operations")
        active_ids: list[str] = []
        for operation in operations:
            item = _object(operation, "active operation")
            active_id = item.get("operation_id")
            if not isinstance(active_id, str) or not active_id:
                raise InvalidResponseError(
                    "Godot editor state has invalid active operation identity"
                )
            active_ids.append(active_id)
        return operation_id in active_ids


@dataclass(frozen=True)
class ReloadStatusPayload:
    """Validated identity-bearing reload-status response."""

    raw: Mapping[str, Any]
    completed: bool
    status: str
    operation_id: str
    project_hash: str
    bridge_version: str

    @classmethod
    def from_payload(cls, payload: Any) -> ReloadStatusPayload:
        raw = _object(payload, "reload status")
        completed = raw.get("completed")
        status = raw.get("status")
        operation_id = raw.get("operation_id")
        project_hash = raw.get("project_hash")
        bridge_version = raw.get("bridge_version")
        if not isinstance(completed, bool):
            raise InvalidResponseError("Godot editor returned invalid reload completion")
        if status not in {"pending", "completed"}:
            raise InvalidResponseError("Godot editor returned invalid reload status")
        if not isinstance(operation_id, str) or not operation_id:
            raise InvalidResponseError("Godot editor returned invalid reload operation ID")
        if (
            not isinstance(project_hash, str)
            or len(project_hash) != 64
            or any(character not in "0123456789abcdef" for character in project_hash)
        ):
            raise InvalidResponseError("Godot editor returned invalid reload project hash")
        if not isinstance(bridge_version, str) or not bridge_version:
            raise InvalidResponseError("Godot editor returned invalid reload bridge version")
        if completed != (status == "completed"):
            raise InvalidResponseError("Godot editor returned inconsistent reload status")
        return cls(
            raw, completed, status, operation_id, project_hash, bridge_version
        )

    def as_dict(self) -> dict[str, Any]:
        return dict(self.raw)


__all__ = ["EditorStatePayload", "ImportPayload", "ReloadStatusPayload"]
