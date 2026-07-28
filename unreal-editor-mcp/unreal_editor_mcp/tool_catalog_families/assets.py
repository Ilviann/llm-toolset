"""Asset reference and deletion tools."""

from __future__ import annotations

from typing import Final

from .schemas import _ASSET_OBJECT_PATH, _OPERATION_ID, _SNAPSHOT_ID

ASSET_TOOLS: Final = (
    {
        "name": "asset_references",
        "description": "Find bounded serialized, management, searchable-name, and live-memory referencers for one exact mounted asset.",
        "inputSchema": {
            "oneOf": [
                {
                    "type": "object",
                    "properties": {
                        "asset_path": _ASSET_OBJECT_PATH,
                        "page_size": {"type": "integer", "minimum": 1, "maximum": 100},
                    },
                    "required": ["asset_path"],
                    "additionalProperties": False,
                },
                {
                    "type": "object",
                    "properties": {
                        "cursor": _OPERATION_ID,
                        "page_size": {"type": "integer", "minimum": 1, "maximum": 100},
                    },
                    "required": ["cursor"],
                    "additionalProperties": False,
                },
            ]
        },
    },
    {
        "name": "asset_delete",
        "description": "Delete one exact unreferenced project asset after stale-safe reference and editor-state preflight.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "operation_id": _OPERATION_ID,
                "asset_path": _ASSET_OBJECT_PATH,
                "expected_snapshot": _SNAPSHOT_ID,
            },
            "required": ["operation_id", "asset_path", "expected_snapshot"],
            "additionalProperties": False,
        },
    },
)
