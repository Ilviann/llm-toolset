"""Level discovery and opening tools."""

from __future__ import annotations

from typing import Final

from .schemas import _OPERATION_ID, _PATH

LEVEL_TOOLS: Final = (
    {
        "name": "level_inspect",
        "description": "Discover mounted World assets or report the exact current-map identity, revision, dirty state, and snapshot.",
        "inputSchema": {
            "oneOf": [
                {
                    "type": "object",
                    "properties": {
                        "mode": {"const": "discover"},
                        "package_path": {
                            "type": "string",
                            "minLength": 1,
                            "maxLength": 512,
                            "pattern": r"^(?!.*\.\.)/[^\\]*$",
                        },
                        "asset_name": {"type": "string", "minLength": 1, "maxLength": 128},
                        "page_size": {"type": "integer", "minimum": 1, "maximum": 100},
                    },
                    "required": ["mode"],
                    "additionalProperties": False,
                },
                {
                    "type": "object",
                    "properties": {"mode": {"const": "current"}},
                    "required": ["mode"],
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
        "name": "level_open",
        "description": "Safely open one exact mounted World asset without implicitly saving, discarding, or prompting.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "operation_id": _OPERATION_ID,
                "map_path": _PATH,
            },
            "required": ["operation_id", "map_path"],
            "additionalProperties": False,
        },
    },
)
