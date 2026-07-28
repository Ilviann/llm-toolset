"""Gameplay-framework project-setting tools."""

from __future__ import annotations

from typing import Final

from .schemas import _OPERATION_ID, _PATH, _PROJECT_HASH

GAMEPLAY_TOOLS: Final = (
    {
        "name": "gameplay_framework_edit",
        "description": "Assign only this project's default GameMode or GameInstance class with a stale-value precondition and verified config persistence.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "operation_id": _OPERATION_ID,
                "project_hash": _PROJECT_HASH,
                "setting": {"type": "string", "enum": ["default_game_mode", "default_game_instance"]},
                "class_path": _PATH,
                "expected_class": {
                    "type": "string",
                    "maxLength": 512,
                    "pattern": r"^(|/(?!.*\.\.)[^\\]+)$",
                },
            },
            "required": ["operation_id", "project_hash", "setting", "class_path", "expected_class"],
            "additionalProperties": False,
        },
    },
)
