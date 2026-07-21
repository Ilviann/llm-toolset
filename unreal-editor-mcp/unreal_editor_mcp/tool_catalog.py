"""Small static tool catalog for the released Actor Blueprint surface."""

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
    {
        "name": "blueprint_inspect",
        "description": "Discover Actor Blueprints or inspect selected structure through bounded snapshot pages.",
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
                            "pattern": "^(?!.*\\.\\.)/[^\\\\]*$",
                        },
                        "asset_name": {"type": "string", "minLength": 1, "maxLength": 128},
                        "page_size": {"type": "integer", "minimum": 1, "maximum": 100},
                    },
                    "required": ["mode"],
                    "additionalProperties": False,
                },
                {
                    "type": "object",
                    "properties": {
                        "mode": {"const": "inspect"},
                        "asset_path": {
                            "type": "string",
                            "minLength": 3,
                            "maxLength": 512,
                            "pattern": "^(?!.*\\.\\.)/[^\\\\]+$",
                        },
                        "sections": {
                            "type": "array",
                            "minItems": 1,
                            "maxItems": 9,
                            "items": {
                                "type": "string",
                                "enum": [
                                    "summary",
                                    "parent_class",
                                    "compile_state",
                                    "components",
                                    "variables",
                                    "graphs",
                                    "nodes",
                                    "pins",
                                    "connections",
                                ],
                            },
                        },
                        "graph_id": {"type": "string", "minLength": 32, "maxLength": 32},
                        "include_inherited": {"type": "boolean"},
                        "page_size": {"type": "integer", "minimum": 1, "maximum": 100},
                    },
                    "required": ["mode", "asset_path"],
                    "additionalProperties": False,
                },
                {
                    "type": "object",
                    "properties": {
                        "cursor": {"type": "string", "minLength": 32, "maxLength": 32},
                        "page_size": {"type": "integer", "minimum": 1, "maximum": 100},
                    },
                    "required": ["cursor"],
                    "additionalProperties": False,
                },
            ]
        },
    },
    {
        "name": "blueprint_create",
        "description": "Create, compile, save, and verify one new Actor Blueprint without overwriting content.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "parent_class": {
                    "type": "string",
                    "minLength": 3,
                    "maxLength": 512,
                    "pattern": "^(?!.*\\.\\.)/[^\\\\]+$",
                },
                "package_path": {
                    "type": "string",
                    "minLength": 3,
                    "maxLength": 512,
                    "pattern": "^(?!.*\\.)(?!.*\\.\\.)/[^\\\\]+$",
                },
            },
            "required": ["parent_class", "package_path"],
            "additionalProperties": False,
        },
    },
    {
        "name": "blueprint_compile",
        "description": "Compile one mutable Actor Blueprint and return bounded structured diagnostics.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "asset_path": {
                    "type": "string",
                    "minLength": 3,
                    "maxLength": 512,
                    "pattern": "^(?!.*\\.\\.)/[^\\\\]+$",
                },
            },
            "required": ["asset_path"],
            "additionalProperties": False,
        },
    },
    {
        "name": "blueprint_save",
        "description": "Save one mutable Actor Blueprint package non-interactively and verify its snapshot.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "asset_path": {
                    "type": "string",
                    "minLength": 3,
                    "maxLength": 512,
                    "pattern": "^(?!.*\\.\\.)/[^\\\\]+$",
                },
            },
            "required": ["asset_path"],
            "additionalProperties": False,
        },
    },
)
TOOL_BY_NAME: Final = {tool["name"]: tool for tool in TOOLS}
