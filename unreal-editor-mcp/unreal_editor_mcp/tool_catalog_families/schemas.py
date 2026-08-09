"""Shared exact JSON Schema fragments for Unreal Editor MCP tools."""

from __future__ import annotations

_PATH = {
    "type": "string",
    "minLength": 3,
    "maxLength": 512,
    "pattern": r"^(?!.*\.\.)/[^\\]+$",
}
_ASSET_OBJECT_PATH = {
    "type": "string",
    "minLength": 3,
    "maxLength": 512,
    "pattern": r"^(?!.*\.\.)/(?:[^\\/:]+/)*[^\\/:.]+\.[^\\/:.]+$",
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
_PROJECT_HASH = _SNAPSHOT_ID
_COMPONENT_ID = {
    "type": "string",
    "minLength": 32,
    "maxLength": 32,
    "pattern": "^[0-9a-f]{32}$",
}
_MEMBER_ID = _COMPONENT_ID
_FUNCTION_ID = _COMPONENT_ID
_LOCAL_ID = _COMPONENT_ID
_MACRO_ID = _COMPONENT_ID
_CUSTOM_EVENT_ID = _COMPONENT_ID
_NODE_ID = _COMPONENT_ID
_PIN_ID = _COMPONENT_ID
_ACTION_ID = _COMPONENT_ID
_FUNCTION_FINGERPRINT = _SNAPSHOT_ID
_STRUCT_MEMBER_ID = _COMPONENT_ID
_GRAPH_POSITION = {
    "type": "object",
    "properties": {
        "x": {"type": "integer", "minimum": -1000000, "maximum": 1000000},
        "y": {"type": "integer", "minimum": -1000000, "maximum": 1000000},
    },
    "required": ["x", "y"],
    "additionalProperties": False,
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
_BLOCK_NODE_KEY = {
    "type": "string",
    "minLength": 1,
    "maxLength": 64,
    "pattern": r"^[A-Za-z][A-Za-z0-9_]*$",
}
_BLOCK_PIN_ENDPOINT = {
    "type": "object",
    "properties": {
        "node_key": {
            "type": "string",
            "minLength": 1,
            "maxLength": 64,
            "pattern": r"^(?:\$entry|\$result|[A-Za-z][A-Za-z0-9_]*)$",
        },
        "pin_name": {"type": "string", "minLength": 1, "maxLength": 128},
    },
    "required": ["node_key", "pin_name"],
    "additionalProperties": False,
}
_BLOCK_NODE = {
    "type": "object",
    "properties": {
        "key": _BLOCK_NODE_KEY,
        "action_id": _ACTION_ID,
        "position": _GRAPH_POSITION,
    },
    "required": ["key", "action_id", "position"],
    "additionalProperties": False,
}
_BLOCK_PIN_DEFAULT = {
    "type": "object",
    "properties": {
        "endpoint": _BLOCK_PIN_ENDPOINT,
        "value": _K2_DEFAULT,
    },
    "required": ["endpoint", "value"],
    "additionalProperties": False,
}
_BLOCK_DIRECT_CONNECTION = {
    "type": "object",
    "properties": {
        "from": _BLOCK_PIN_ENDPOINT,
        "to": _BLOCK_PIN_ENDPOINT,
    },
    "required": ["from", "to"],
    "additionalProperties": False,
}
_BLOCK_CONVERTED_CONNECTION = {
    "type": "object",
    "properties": {
        "from": _BLOCK_PIN_ENDPOINT,
        "to": _BLOCK_PIN_ENDPOINT,
        "automatic_conversion": {"const": True},
        "conversion_position": _GRAPH_POSITION,
    },
    "required": ["from", "to", "automatic_conversion", "conversion_position"],
    "additionalProperties": False,
}
_BLOCK_CONNECTION = {
    "oneOf": [_BLOCK_DIRECT_CONNECTION, _BLOCK_CONVERTED_CONNECTION],
}
_BLOCK_EXTERNAL_ENDPOINT = {
    "type": "object",
    "properties": {"node_id": _NODE_ID, "pin_id": _PIN_ID},
    "required": ["node_id", "pin_id"],
    "additionalProperties": False,
}
_BLOCK_EXTERNAL_CONNECTION = {
    "oneOf": [
        {
            "type": "object",
            "properties": {"from": _BLOCK_PIN_ENDPOINT, "to": _BLOCK_EXTERNAL_ENDPOINT},
            "required": ["from", "to"],
            "additionalProperties": False,
        },
        {
            "type": "object",
            "properties": {"from": _BLOCK_EXTERNAL_ENDPOINT, "to": _BLOCK_PIN_ENDPOINT},
            "required": ["from", "to"],
            "additionalProperties": False,
        },
    ],
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
_CUSTOM_EVENT_METADATA = {
    "type": "object",
    "properties": {
        **_FUNCTION_METADATA["properties"],
        "rpc_mode": {
            "type": "string",
            "enum": ["not_replicated", "server", "client", "multicast"],
        },
        "reliability": {"type": "string", "enum": ["unreliable", "reliable"]},
    },
    "minProperties": 1,
    "additionalProperties": False,
}
_MACRO_METADATA = {
    "type": "object",
    "properties": {
        "category": {"type": "string", "maxLength": 128},
        "tooltip": {"type": "string", "maxLength": 512},
        "keywords": {"type": "string", "maxLength": 256},
    },
    "minProperties": 1,
    "additionalProperties": False,
}

_MACRO_SIGNATURE = {
    "type": "object",
    "properties": {
        "pure": {"type": "boolean"},
        "parameters": {"type": "array", "maxItems": 32, "items": _FUNCTION_PARAMETER},
    },
    "required": ["pure", "parameters"],
    "additionalProperties": False,
}
_CUSTOM_EVENT_PARAMETER = {
    "type": "object",
    "properties": {
        "name": {"type": "string", "minLength": 1, "maxLength": 128},
        "type": _K2_TYPE,
        "default": _K2_DEFAULT,
    },
    "required": ["name", "type"],
    "additionalProperties": False,
}
_CUSTOM_EVENT_SIGNATURE = {
    "type": "object",
    "properties": {
        "parameters": {"type": "array", "maxItems": 32, "items": _CUSTOM_EVENT_PARAMETER},
    },
    "required": ["parameters"],
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


def _graph_shape(operation: str, required: list[str], **extra: object) -> dict[str, object]:
    return {
        "type": "object",
        "properties": _mutation_properties(
            operation={"const": operation}, graph_id=_COMPONENT_ID, **extra
        ),
        "required": [
            "operation_id", "asset_path", "expected_snapshot", "operation", "graph_id", *required,
        ],
        "additionalProperties": False,
    }
