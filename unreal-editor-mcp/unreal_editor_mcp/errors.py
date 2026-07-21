"""Stable bounded errors shared by the MCP and bridge boundaries."""

from __future__ import annotations

from enum import Enum
from typing import Any, Mapping


MAX_ERROR_MESSAGE = 512
MAX_ERROR_DETAILS = 16


class ErrorCode(str, Enum):
    INVALID_ARGUMENT = "invalid_argument"
    INVALID_CONFIGURATION = "invalid_configuration"
    EDITOR_UNAVAILABLE = "editor_unavailable"
    AUTHENTICATION_FAILED = "authentication_failed"
    REQUEST_TOO_LARGE = "request_too_large"
    RESPONSE_TOO_LARGE = "response_too_large"
    TIMEOUT = "timeout"
    CANCELLED = "cancelled"
    VERSION_MISMATCH = "version_mismatch"
    INVALID_RESPONSE = "invalid_response"
    BUSY = "busy"
    NOT_FOUND = "not_found"
    WRONG_TYPE = "wrong_type"
    ALREADY_EXISTS = "already_exists"
    INVALID_PARENT = "invalid_parent"
    MUTATION_SCOPE_DENIED = "mutation_scope_denied"
    COMPILE_FAILED = "compile_failed"
    SAVE_FAILED = "save_failed"
    WRITE_CONFLICT = "write_conflict"
    CURSOR_EXPIRED = "cursor_expired"
    STALE_PRECONDITION = "stale_precondition"
    OPERATION_CONFLICT = "operation_conflict"
    OUTCOME_UNKNOWN = "outcome_unknown"
    UNSUPPORTED_PROPERTY = "unsupported_property"
    INVALID_COMPONENT = "invalid_component"
    INVALID_MEMBER = "invalid_member"
    UNSUPPORTED_TYPE = "unsupported_type"
    REFERENCED_MEMBER = "referenced_member"
    INTERNAL_ERROR = "internal_error"


def _bounded_message(value: object) -> str:
    text = str(value).replace("\x00", "")
    return text[:MAX_ERROR_MESSAGE] or "Unreal Editor MCP error"


def _bounded_details(value: Mapping[str, Any] | None) -> dict[str, Any]:
    if not value:
        return {}
    result: dict[str, Any] = {}
    for key, item in list(value.items())[:MAX_ERROR_DETAILS]:
        if not isinstance(key, str) or len(key) > 64:
            continue
        if item is None or isinstance(item, (bool, int, float)):
            result[key] = item
        elif isinstance(item, str):
            result[key] = item[:MAX_ERROR_MESSAGE]
    return result


class DomainError(Exception):
    """Expected failure safe to return to a model-facing caller."""

    def __init__(
        self,
        message: str,
        *,
        code: ErrorCode = ErrorCode.INTERNAL_ERROR,
        details: Mapping[str, Any] | None = None,
        retryable: bool = False,
    ) -> None:
        super().__init__(_bounded_message(message))
        self.code = code
        self.details = _bounded_details(details)
        self.retryable = bool(retryable)

    def as_dict(self) -> dict[str, Any]:
        return {
            "code": self.code.value,
            "message": str(self),
            "details": dict(self.details),
            "retryable": self.retryable,
        }


class ConfigurationError(DomainError):
    def __init__(self, message: str, **kwargs: Any) -> None:
        super().__init__(message, code=ErrorCode.INVALID_CONFIGURATION, **kwargs)


class BridgeError(DomainError):
    pass


def bridge_error_from_payload(payload: object) -> BridgeError:
    if not isinstance(payload, Mapping):
        return BridgeError(
            "Unreal Editor returned an invalid error",
            code=ErrorCode.INVALID_RESPONSE,
        )
    raw_code = payload.get("code")
    try:
        code = ErrorCode(raw_code)
    except (TypeError, ValueError):
        code = ErrorCode.INTERNAL_ERROR
    message = payload.get("message")
    details = payload.get("details")
    return BridgeError(
        message if isinstance(message, str) else "Unreal Editor request failed",
        code=code,
        details=details if isinstance(details, Mapping) else None,
        retryable=payload.get("retryable") is True,
    )
