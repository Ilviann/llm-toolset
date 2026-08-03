"""Core bridge-state and operation-reconciliation tools."""

from __future__ import annotations

from typing import Final

from .schemas import _OPERATION_ID

CORE_TOOLS: Final = (
    {
        "name": "capabilities",
        "description": "Report configured project identity and exact available bridge, Unreal, command, feature, and limit capabilities.",
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    },
    {
        "name": "editor_state",
        "description": "Report project identity, bridge readiness, editor activity, and queued work.",
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    },
    {
        "name": "operation_status",
        "description": "Resolve one retained operation by operation and bridge-instance identity.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "operation_id": _OPERATION_ID,
                "bridge_instance_id": _OPERATION_ID,
            },
            "required": ["operation_id", "bridge_instance_id"],
            "additionalProperties": False,
        },
    },
    {
        "name": "operation_cancel",
        "description": "Cancel one queued retained mutation by operation and bridge-instance identity.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "operation_id": _OPERATION_ID,
                "bridge_instance_id": _OPERATION_ID,
            },
            "required": ["operation_id", "bridge_instance_id"],
            "additionalProperties": False,
        },
    },
)
