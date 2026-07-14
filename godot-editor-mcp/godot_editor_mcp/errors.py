"""Stable, bounded errors shared by the MCP and Godot bridge boundaries."""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from typing import Any, ClassVar


MAX_DETAIL_DEPTH = 3
MAX_DETAIL_ITEMS = 16
MAX_DETAIL_TEXT = 512


class ErrorCode:
    UNAUTHORIZED = "unauthorized"
    INVALID_ARGUMENT = "invalid_argument"
    PROTECTED_PATH = "protected_path"
    NOT_FOUND = "not_found"
    EDITOR_BUSY = "editor_busy"
    IMPORT_PENDING = "import_pending"
    NO_ACTIVE_RUN = "no_active_run"
    STALE_RUNTIME_ID = "stale_runtime_id"
    TIMEOUT = "timeout"
    UNSUPPORTED_CAPABILITY = "unsupported_capability"
    STALE_CURSOR = "stale_cursor"
    PROJECT_MISMATCH = "project_mismatch"
    EDITOR_UNAVAILABLE = "editor_unavailable"
    INVALID_RESPONSE = "invalid_response"
    REQUEST_TOO_LARGE = "request_too_large"
    RESPONSE_TOO_LARGE = "response_too_large"
    INVALID_CONFIGURATION = "invalid_configuration"
    COMMAND_FAILED = "command_failed"
    SAVE_FAILED = "save_failed"
    MALFORMED_OPERATION = "malformed_operation"
    STALE_OPERATION = "stale_operation"
    VERSION_MISMATCH = "version_mismatch"
    CANCELLED = "cancelled"


def bounded_details(value: Any, depth: int = 0) -> Any:
    """Return a small JSON-safe representation of untrusted error details."""
    if depth >= MAX_DETAIL_DEPTH:
        return "..."
    if value is None or isinstance(value, (bool, int, float)):
        return value
    if isinstance(value, str):
        return value[:MAX_DETAIL_TEXT]
    if isinstance(value, Mapping):
        output: dict[str, Any] = {}
        for key, item in list(value.items())[:MAX_DETAIL_ITEMS]:
            output[str(key)[:128]] = bounded_details(item, depth + 1)
        return output
    if isinstance(value, Sequence) and not isinstance(value, (bytes, bytearray)):
        return [bounded_details(item, depth + 1) for item in value[:MAX_DETAIL_ITEMS]]
    return str(value)[:MAX_DETAIL_TEXT]


class DomainError(Exception):
    """A typed error whose public fields are safe to send to an MCP client."""

    default_code: ClassVar[str] = ErrorCode.COMMAND_FAILED
    default_retryable: ClassVar[bool] = False

    def __init__(
        self,
        message: str,
        *,
        code: str | None = None,
        details: Any = None,
        retryable: bool | None = None,
    ) -> None:
        safe_message = str(message)[:MAX_DETAIL_TEXT] or "Godot command failed"
        super().__init__(safe_message)
        self.code = code or self.default_code
        self.message = safe_message
        self.details = bounded_details({} if details is None else details)
        self.retryable = self.default_retryable if retryable is None else bool(retryable)

    def as_dict(self) -> dict[str, Any]:
        return {
            "code": self.code,
            "message": self.message,
            "details": self.details,
            "retryable": self.retryable,
        }


class BridgeError(DomainError):
    """Base error for the authenticated localhost bridge."""


class UnauthorizedError(BridgeError):
    default_code = ErrorCode.UNAUTHORIZED


class InvalidArgumentError(BridgeError):
    default_code = ErrorCode.INVALID_ARGUMENT


class ProtectedPathError(BridgeError):
    default_code = ErrorCode.PROTECTED_PATH


class NotFoundError(BridgeError):
    default_code = ErrorCode.NOT_FOUND


class EditorBusyError(BridgeError):
    default_code = ErrorCode.EDITOR_BUSY
    default_retryable = True


class ImportPendingError(BridgeError):
    default_code = ErrorCode.IMPORT_PENDING
    default_retryable = True


class NoActiveRunError(BridgeError):
    default_code = ErrorCode.NO_ACTIVE_RUN


class StaleRuntimeIdError(BridgeError):
    default_code = ErrorCode.STALE_RUNTIME_ID


class OperationTimeoutError(BridgeError):
    default_code = ErrorCode.TIMEOUT
    default_retryable = True


class UnsupportedCapabilityError(BridgeError):
    default_code = ErrorCode.UNSUPPORTED_CAPABILITY


class StaleCursorError(BridgeError):
    default_code = ErrorCode.STALE_CURSOR


class ProjectMismatchError(BridgeError):
    default_code = ErrorCode.PROJECT_MISMATCH


class SaveFailedError(BridgeError):
    default_code = ErrorCode.SAVE_FAILED


class MalformedOperationError(BridgeError):
    default_code = ErrorCode.MALFORMED_OPERATION


class StaleOperationError(BridgeError):
    default_code = ErrorCode.STALE_OPERATION


class VersionMismatchError(BridgeError):
    default_code = ErrorCode.VERSION_MISMATCH


class OperationCancelledError(BridgeError):
    default_code = ErrorCode.CANCELLED


_ERROR_TYPES: dict[str, type[BridgeError]] = {
    cls.default_code: cls
    for cls in (
        UnauthorizedError,
        InvalidArgumentError,
        ProtectedPathError,
        NotFoundError,
        EditorBusyError,
        ImportPendingError,
        NoActiveRunError,
        StaleRuntimeIdError,
        OperationTimeoutError,
        UnsupportedCapabilityError,
        StaleCursorError,
        ProjectMismatchError,
        SaveFailedError,
        MalformedOperationError,
        StaleOperationError,
        VersionMismatchError,
        OperationCancelledError,
    )
}


def bridge_error_from_payload(payload: Any) -> BridgeError:
    """Decode both Phase-1 envelopes and legacy string bridge errors."""
    if isinstance(payload, str):
        return BridgeError(payload, code=ErrorCode.COMMAND_FAILED)
    if not isinstance(payload, Mapping):
        return BridgeError("Godot command failed", code=ErrorCode.INVALID_RESPONSE)
    code = payload.get("code")
    message = payload.get("message")
    details = payload.get("details", {})
    retryable = payload.get("retryable", False)
    if not isinstance(code, str) or not isinstance(message, str) or not isinstance(retryable, bool):
        return BridgeError("Godot editor returned an invalid error", code=ErrorCode.INVALID_RESPONSE)
    error_type = _ERROR_TYPES.get(code, BridgeError)
    return error_type(message, code=code, details=details, retryable=retryable)


__all__ = [
    "BridgeError",
    "DomainError",
    "EditorBusyError",
    "ErrorCode",
    "ImportPendingError",
    "InvalidArgumentError",
    "NoActiveRunError",
    "NotFoundError",
    "OperationTimeoutError",
    "OperationCancelledError",
    "MalformedOperationError",
    "ProjectMismatchError",
    "SaveFailedError",
    "ProtectedPathError",
    "StaleCursorError",
    "StaleRuntimeIdError",
    "StaleOperationError",
    "UnauthorizedError",
    "UnsupportedCapabilityError",
    "VersionMismatchError",
    "bounded_details",
    "bridge_error_from_payload",
]
