"""Asset reference and deletion tools."""

from __future__ import annotations

from typing import Final

from .schemas import _ASSET_OBJECT_PATH, _OPERATION_ID, _SNAPSHOT_ID

_ASSET_INSPECT_PATH = {
    "type": "string",
    "minLength": 7,
    "maxLength": 512,
    "pattern": r"^/Game/(?:[^\\/:.]+/)*[^\\/:.]+(?:\.[^\\/:.]+)?$",
}
_ASSET_SELECTOR = {
    "type": "string",
    "minLength": 1,
    "maxLength": 1024,
    "pattern": r"^(?:[A-Za-z0-9._~-]|%[0-9A-F]{2})+(?:/(?:[A-Za-z0-9._~-]|%[0-9A-F]{2})+)*$",
}

ASSET_TOOLS: Final = (
    {
        "name": "asset_inspect",
        "description": "Inspect one exact /Game asset through bounded semantic YAML roots and hierarchical selectors.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "asset_path": _ASSET_INSPECT_PATH,
                "selector": _ASSET_SELECTOR,
                "verbose": {"type": "boolean"},
                "page_size": {"type": "integer", "minimum": 1, "maximum": 100},
                "page_index": {"type": "integer", "minimum": 0, "maximum": 1000000},
                "allow_partial_graph": {"type": "boolean"},
            },
            "required": ["asset_path"],
            "additionalProperties": False,
        },
    },
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
        "description": "Delete one exact unreferenced project asset or complete map-owned package closure after stale-safe preflight.",
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
