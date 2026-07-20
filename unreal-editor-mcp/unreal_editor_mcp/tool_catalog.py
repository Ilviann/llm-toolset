"""Small static Phase 1 tool catalog."""

from __future__ import annotations

from typing import Final


SUPPORTED_PROTOCOLS: Final = ("2024-11-05", "2025-03-26", "2025-06-18")
LATEST_PROTOCOL: Final = SUPPORTED_PROTOCOLS[-1]

TOOLS: Final = (
    {
        "name": "capabilities",
        "description": "Report exact bridge, Unreal, command, feature, and limit capabilities.",
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    },
    {
        "name": "editor_state",
        "description": "Report project identity, bridge readiness, editor activity, and queued work.",
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    },
)
TOOL_BY_NAME: Final = {tool["name"]: tool for tool in TOOLS}
