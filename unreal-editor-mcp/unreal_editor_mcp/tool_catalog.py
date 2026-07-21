"""Small static tool catalog for the released Actor Blueprint surface."""

from __future__ import annotations

from typing import Final


SUPPORTED_PROTOCOLS: Final = ("2024-11-05", "2025-03-26", "2025-06-18")
LATEST_PROTOCOL: Final = SUPPORTED_PROTOCOLS[-1]

_PATH = {
    "type": "string",
    "minLength": 3,
    "maxLength": 512,
    "pattern": r"^(?!.*\.\.)/[^\\]+$",
}
_OPERATION_ID = {
    "type": "string",
    "minLength": 32,
    "maxLength": 32,
    "pattern": "^[0-9a-f]{32}$",
}
_SNAPSHOT_ID = {
    "type": "string",
    "minLength": 40,
    "maxLength": 40,
    "pattern": "^[0-9a-f]{40}$",
}
_COMPONENT_ID = {
    "type": "string",
    "minLength": 32,
    "maxLength": 32,
    "pattern": "^[0-9a-f]{32}$",
}
_PROPERTY_VALUE = {
    "oneOf": [
        {"type": "boolean"},
        {"type": "number"},
        {"type": "string", "maxLength": 4096},
        {
            "type": "array",
            "maxItems": 64,
            "items": {"type": "string", "minLength": 1, "maxLength": 128},
        },
    ]
}


def _mutation_properties(**extra: object) -> dict[str, object]:
    return {
        "operation_id": _OPERATION_ID,
        "asset_path": _PATH,
        "expected_snapshot": _SNAPSHOT_ID,
        **extra,
    }


def _component_shape(operation: str, required: list[str], **extra: object) -> dict[str, object]:
    return {
        "type": "object",
        "properties": _mutation_properties(operation={"const": operation}, **extra),
        "required": ["operation_id", "asset_path", "expected_snapshot", "operation", *required],
        "additionalProperties": False,
    }


TOOLS: Final = (
    {
        "name": "capabilities",
        "description": "Report exact bridge, Unreal, command, feature, identity, and limit capabilities.",
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    },
    {
        "name": "editor_state",
        "description": "Report project identity, bridge readiness, editor activity, and queued work.",
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    },
    {
        "name": "operation_status",
        "description": "Resolve or cancel one retained mutation by operation and bridge-instance identity.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "operation_id": _OPERATION_ID,
                "bridge_instance_id": _OPERATION_ID,
                "cancel": {"type": "boolean"},
            },
            "required": ["operation_id", "bridge_instance_id"],
            "additionalProperties": False,
        },
    },
    {
        "name": "blueprint_inspect",
        "description": "Discover Actor Blueprints or inspect selected structure and editable defaults through bounded snapshot pages.",
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
                    "properties": {
                        "mode": {"const": "inspect"},
                        "asset_path": _PATH,
                        "sections": {
                            "type": "array",
                            "minItems": 1,
                            "maxItems": 10,
                            "items": {
                                "type": "string",
                                "enum": [
                                    "summary", "parent_class", "compile_state", "components",
                                    "class_defaults", "variables", "graphs", "nodes", "pins", "connections",
                                ],
                            },
                        },
                        "graph_id": _COMPONENT_ID,
                        "component_id": _COMPONENT_ID,
                        "property_names": {
                            "type": "array",
                            "minItems": 1,
                            "maxItems": 32,
                            "items": {"type": "string", "minLength": 1, "maxLength": 128},
                        },
                        "include_inherited": {"type": "boolean"},
                        "page_size": {"type": "integer", "minimum": 1, "maximum": 100},
                    },
                    "required": ["mode", "asset_path"],
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
        "name": "blueprint_create",
        "description": "Reliably create, compile, save, and verify one new Actor Blueprint without overwriting content.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "operation_id": _OPERATION_ID,
                "parent_class": _PATH,
                "package_path": {
                    "type": "string",
                    "minLength": 3,
                    "maxLength": 512,
                    "pattern": r"^(?!.*\.)(?!.*\.\.)/[^\\]+$",
                },
            },
            "required": ["operation_id", "parent_class", "package_path"],
            "additionalProperties": False,
        },
    },
    {
        "name": "blueprint_compile",
        "description": "Reliably compile one current Actor Blueprint snapshot and return bounded diagnostics.",
        "inputSchema": {
            "type": "object",
            "properties": _mutation_properties(),
            "required": ["operation_id", "asset_path", "expected_snapshot"],
            "additionalProperties": False,
        },
    },
    {
        "name": "blueprint_save",
        "description": "Reliably save one current Actor Blueprint snapshot non-interactively.",
        "inputSchema": {
            "type": "object",
            "properties": _mutation_properties(),
            "required": ["operation_id", "asset_path", "expected_snapshot"],
            "additionalProperties": False,
        },
    },
    {
        "name": "blueprint_component_edit",
        "description": "Perform one reconciled Actor Blueprint component hierarchy or component-default edit.",
        "inputSchema": {
            "oneOf": [
                _component_shape(
                    "add", ["component_class", "name"],
                    component_class=_PATH,
                    name={"type": "string", "minLength": 1, "maxLength": 128},
                    parent_id=_COMPONENT_ID,
                ),
                _component_shape("remove", ["component_id"], component_id=_COMPONENT_ID),
                _component_shape(
                    "rename", ["component_id", "new_name"], component_id=_COMPONENT_ID,
                    new_name={"type": "string", "minLength": 1, "maxLength": 128},
                ),
                _component_shape(
                    "reparent", ["component_id", "new_parent_id"],
                    component_id=_COMPONENT_ID, new_parent_id=_COMPONENT_ID,
                ),
                _component_shape("set_root", ["component_id"], component_id=_COMPONENT_ID),
                _component_shape(
                    "set_property", ["component_id", "property_name", "value"],
                    component_id=_COMPONENT_ID,
                    property_name={"type": "string", "minLength": 1, "maxLength": 128},
                    value=_PROPERTY_VALUE,
                ),
            ]
        },
    },
    {
        "name": "blueprint_default_edit",
        "description": "Set one supported editable property on an Actor Blueprint generated-class default object.",
        "inputSchema": {
            "type": "object",
            "properties": _mutation_properties(
                property_name={"type": "string", "minLength": 1, "maxLength": 128},
                value=_PROPERTY_VALUE,
            ),
            "required": ["operation_id", "asset_path", "expected_snapshot", "property_name", "value"],
            "additionalProperties": False,
        },
    },
)
TOOL_BY_NAME: Final = {tool["name"]: tool for tool in TOOLS}
