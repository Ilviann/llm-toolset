"""Opt-in editor lifecycle tool."""

from __future__ import annotations

from typing import Final

from .schemas import _OPERATION_ID

EDITOR_LIFECYCLE_TOOL: Final = {
    "name": "editor_lifecycle",
    "description": "Launch, gracefully shut down, restart, or cancel waiting for only the startup-configured Unreal Editor project.",
    "inputSchema": {
        "type": "object",
        "properties": {
            "operation_id": _OPERATION_ID,
            "operation": {"type": "string", "enum": ["launch", "shutdown", "restart", "cancel"]},
        },
        "required": ["operation_id", "operation"],
        "additionalProperties": False,
    },
}
