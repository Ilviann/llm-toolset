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
_MEMBER_ID = _COMPONENT_ID
_FUNCTION_ID = _COMPONENT_ID
_LOCAL_ID = _COMPONENT_ID
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

_K2_TERMINAL = {
    "type": "object",
    "properties": {
        "category": {
            "type": "string",
            "enum": [
                "boolean", "byte", "int", "int64", "real", "name", "string", "text",
                "enum", "struct", "object", "class", "softobject", "softclass",
            ],
        },
        "subcategory": {"type": "string", "maxLength": 64},
        "type_object": _PATH,
    },
    "required": ["category"],
    "additionalProperties": False,
}
_K2_TYPE = {
    "type": "object",
    "properties": {
        **_K2_TERMINAL["properties"],
        "container": {"type": "string", "enum": ["none", "array", "set", "map"]},
        "value_type": _K2_TERMINAL,
        "reference": {"type": "boolean"},
        "const": {"type": "boolean"},
    },
    "required": ["category", "container"],
    "additionalProperties": False,
}
_DEFAULT_ATOM = {
    "oneOf": [
        {
            "type": "object",
            "properties": {
                "kind": {"const": "literal"},
                "value": {"oneOf": [{"type": "boolean"}, {"type": "number"}, {"type": "string", "maxLength": 4096}]},
            },
            "required": ["kind", "value"],
            "additionalProperties": False,
        },
        {
            "type": "object",
            "properties": {
                "kind": {"const": "reference"},
                "path": {"type": "string", "maxLength": 512, "pattern": r"^(|/(?!.*\.\.)[^\\]+)$"},
            },
            "required": ["kind", "path"],
            "additionalProperties": False,
        },
    ]
}
_K2_DEFAULT = {
    "oneOf": [
        {
            "type": "object",
            "properties": {"kind": {"const": "engine_default"}},
            "required": ["kind"],
            "additionalProperties": False,
        },
        *_DEFAULT_ATOM["oneOf"],
        {
            "type": "object",
            "properties": {
                "kind": {"type": "string", "enum": ["array", "set"]},
                "items": {"type": "array", "maxItems": 64, "items": _DEFAULT_ATOM},
            },
            "required": ["kind", "items"],
            "additionalProperties": False,
        },
        {
            "type": "object",
            "properties": {
                "kind": {"const": "map"},
                "entries": {
                    "type": "array",
                    "maxItems": 64,
                    "items": {
                        "type": "object",
                        "properties": {"key": _DEFAULT_ATOM, "value": _DEFAULT_ATOM},
                        "required": ["key", "value"],
                        "additionalProperties": False,
                    },
                },
            },
            "required": ["kind", "entries"],
            "additionalProperties": False,
        },
    ]
}
_MEMBER_METADATA = {
    "type": "object",
    "properties": {
        "category": {"type": "string", "maxLength": 128},
        "tooltip": {"type": "string", "maxLength": 512},
        "instance_editable": {"type": "boolean"},
        "blueprint_visible": {"type": "boolean"},
        "blueprint_read_only": {"type": "boolean"},
        "expose_on_spawn": {"type": "boolean"},
        "private": {"type": "boolean"},
        "save_game": {"type": "boolean"},
        "advanced_display": {"type": "boolean"},
        "replication": {"type": "string", "enum": ["none", "replicated", "rep_notify"]},
        "rep_notify_function": {"type": "string", "minLength": 1, "maxLength": 128},
        "replication_condition": {"type": "string", "minLength": 1, "maxLength": 64},
    },
    "minProperties": 1,
    "additionalProperties": False,
}

_FUNCTION_PARAMETER = {
    "type": "object",
    "properties": {
        "name": {"type": "string", "minLength": 1, "maxLength": 128},
        "direction": {"type": "string", "enum": ["input", "output"]},
        "type": _K2_TYPE,
        "default": _K2_DEFAULT,
    },
    "required": ["name", "direction", "type"],
    "additionalProperties": False,
}
_FUNCTION_SIGNATURE = {
    "type": "object",
    "properties": {
        "access": {"type": "string", "enum": ["public", "protected", "private"]},
        "pure": {"type": "boolean"},
        "const": {"type": "boolean"},
        "parameters": {"type": "array", "maxItems": 32, "items": _FUNCTION_PARAMETER},
    },
    "required": ["access", "pure", "const", "parameters"],
    "additionalProperties": False,
}
_FUNCTION_METADATA = {
    "type": "object",
    "properties": {
        "category": {"type": "string", "maxLength": 128},
        "tooltip": {"type": "string", "maxLength": 512},
        "keywords": {"type": "string", "maxLength": 256},
        "call_in_editor": {"type": "boolean"},
    },
    "minProperties": 1,
    "additionalProperties": False,
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


def _member_shape(operation: str, required: list[str], **extra: object) -> dict[str, object]:
    return {
        "type": "object",
        "properties": _mutation_properties(operation={"const": operation}, **extra),
        "required": ["operation_id", "asset_path", "expected_snapshot", "operation", *required],
        "additionalProperties": False,
    }


def _scoped_member_shape(target: str, operation: str, required: list[str], **extra: object) -> dict[str, object]:
    return {
        "type": "object",
        "properties": _mutation_properties(
            target={"const": target}, operation={"const": operation}, **extra
        ),
        "required": ["operation_id", "asset_path", "expected_snapshot", "target", "operation", *required],
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
                            "maxItems": 13,
                            "items": {
                                "type": "string",
                                "enum": [
                                    "summary", "parent_class", "compile_state", "components",
                                    "class_defaults", "variables", "functions", "parameters", "local_variables",
                                    "graphs", "nodes", "pins", "connections",
                                ],
                            },
                        },
                        "graph_id": _COMPONENT_ID,
                        "component_id": _COMPONENT_ID,
                        "member_id": _MEMBER_ID,
                        "function_id": _FUNCTION_ID,
                        "local_id": _LOCAL_ID,
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
    {
        "name": "blueprint_member_edit",
        "description": "Safely edit one Actor Blueprint member variable, user function shell/signature, or function-local variable.",
        "inputSchema": {
            "oneOf": [
                _member_shape(
                    "add", ["name", "type"],
                    name={"type": "string", "minLength": 1, "maxLength": 128},
                    type=_K2_TYPE,
                    default=_K2_DEFAULT,
                    metadata=_MEMBER_METADATA,
                ),
                _member_shape(
                    "rename", ["member_id", "new_name"],
                    member_id=_MEMBER_ID,
                    new_name={"type": "string", "minLength": 1, "maxLength": 128},
                ),
                _member_shape(
                    "update", ["member_id", "field", "type", "policy"],
                    member_id=_MEMBER_ID,
                    field={"const": "type"},
                    type=_K2_TYPE,
                    policy={"const": "reject_if_referenced"},
                ),
                _member_shape(
                    "update", ["member_id", "field", "default"],
                    member_id=_MEMBER_ID,
                    field={"const": "default"},
                    default=_K2_DEFAULT,
                ),
                _member_shape(
                    "update", ["member_id", "field", "metadata"],
                    member_id=_MEMBER_ID,
                    field={"const": "metadata"},
                    metadata=_MEMBER_METADATA,
                ),
                _member_shape(
                    "remove", ["member_id", "policy"],
                    member_id=_MEMBER_ID,
                    policy={"const": "reject_if_referenced"},
                ),
                _scoped_member_shape(
                    "function", "add", ["name", "signature"],
                    name={"type": "string", "minLength": 1, "maxLength": 128},
                    signature=_FUNCTION_SIGNATURE,
                    metadata=_FUNCTION_METADATA,
                ),
                _scoped_member_shape(
                    "function", "rename", ["function_id", "new_name"],
                    function_id=_FUNCTION_ID,
                    new_name={"type": "string", "minLength": 1, "maxLength": 128},
                ),
                _scoped_member_shape(
                    "function", "update", ["function_id", "field", "signature", "policy"],
                    function_id=_FUNCTION_ID,
                    field={"const": "signature"},
                    signature=_FUNCTION_SIGNATURE,
                    policy={"const": "reject_if_referenced"},
                ),
                _scoped_member_shape(
                    "function", "update", ["function_id", "field", "metadata"],
                    function_id=_FUNCTION_ID,
                    field={"const": "metadata"},
                    metadata=_FUNCTION_METADATA,
                ),
                _scoped_member_shape(
                    "function", "remove", ["function_id", "policy"],
                    function_id=_FUNCTION_ID,
                    policy={"const": "reject_if_referenced"},
                ),
                _scoped_member_shape(
                    "local_variable", "add", ["function_id", "name", "type"],
                    function_id=_FUNCTION_ID,
                    name={"type": "string", "minLength": 1, "maxLength": 128},
                    type=_K2_TYPE,
                    default=_K2_DEFAULT,
                ),
                _scoped_member_shape(
                    "local_variable", "rename", ["function_id", "local_id", "new_name"],
                    function_id=_FUNCTION_ID,
                    local_id=_LOCAL_ID,
                    new_name={"type": "string", "minLength": 1, "maxLength": 128},
                ),
                _scoped_member_shape(
                    "local_variable", "update", ["function_id", "local_id", "field", "type", "policy"],
                    function_id=_FUNCTION_ID,
                    local_id=_LOCAL_ID,
                    field={"const": "type"},
                    type=_K2_TYPE,
                    policy={"const": "reject_if_referenced"},
                ),
                _scoped_member_shape(
                    "local_variable", "update", ["function_id", "local_id", "field", "default"],
                    function_id=_FUNCTION_ID,
                    local_id=_LOCAL_ID,
                    field={"const": "default"},
                    default=_K2_DEFAULT,
                ),
                _scoped_member_shape(
                    "local_variable", "remove", ["function_id", "local_id", "policy"],
                    function_id=_FUNCTION_ID,
                    local_id=_LOCAL_ID,
                    policy={"const": "reject_if_referenced"},
                ),
            ]
        },
    },
)
TOOL_BY_NAME: Final = {tool["name"]: tool for tool in TOOLS}
