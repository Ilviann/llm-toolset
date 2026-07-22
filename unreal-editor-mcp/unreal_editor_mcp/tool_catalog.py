"""Small static tool catalog for the released Blueprint-family surface."""

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


def _game_data_value(depth: int = 4) -> dict[str, object]:
    scalar: list[dict[str, object]] = [
        {"type": "boolean"},
        {"type": "number"},
        {"type": "string", "maxLength": 4096},
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
    if depth <= 0:
        return {"oneOf": scalar}
    child = _game_data_value(depth - 1)
    fields = {
        "type": "object",
        "maxProperties": 64,
        "propertyNames": {"type": "string", "minLength": 1, "maxLength": 128},
        "additionalProperties": child,
    }
    return {
        "oneOf": [
            *scalar,
            {"type": "array", "maxItems": 64, "items": child},
            {
                "type": "object",
                "properties": {"kind": {"const": "struct"}, "fields": fields},
                "required": ["kind", "fields"],
                "additionalProperties": False,
            },
            {
                "type": "object",
                "properties": {
                    "kind": {"const": "set"},
                    "items": {"type": "array", "maxItems": 64, "items": child},
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
                            "properties": {"key": child, "value": child},
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


_GAME_DATA_VALUE = _game_data_value()
_ROW_VALUES = {
    "type": "object",
    "maxProperties": 64,
    "propertyNames": {"type": "string", "minLength": 1, "maxLength": 128},
    "additionalProperties": _GAME_DATA_VALUE,
}
_STRUCT_MEMBER = {
    "type": "object",
    "properties": {
        "name": {"type": "string", "minLength": 1, "maxLength": 128},
        "type": _K2_TYPE,
        "default": _K2_DEFAULT,
        "tooltip": {"type": "string", "maxLength": 512},
    },
    "required": ["name", "type"],
    "additionalProperties": False,
}
_ROW_WRITE = {
    "type": "object",
    "properties": {
        "row_name": {"type": "string", "minLength": 1, "maxLength": 128},
        "values": _ROW_VALUES,
        "preserve_unspecified": {"type": "boolean"},
    },
    "required": ["row_name", "values"],
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
        "description": "Discover supported Blueprint families or inspect selected structure, family capabilities, and editable defaults through bounded snapshot pages.",
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
                            "maxItems": 15,
                            "items": {
                                "type": "string",
                                "enum": [
                                    "summary", "parent_class", "compile_state", "components",
                                    "class_defaults", "variables", "functions", "macros", "custom_events",
                                    "parameters", "local_variables",
                                    "graphs", "nodes", "pins", "connections",
                                ],
                            },
                        },
                        "graph_id": _COMPONENT_ID,
                        "component_id": _COMPONENT_ID,
                        "member_id": _MEMBER_ID,
                        "function_id": _FUNCTION_ID,
                        "local_id": _LOCAL_ID,
                        "macro_id": _MACRO_ID,
                        "custom_event_id": _CUSTOM_EVENT_ID,
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
        "name": "blueprint_action_catalog",
        "description": "Discover bounded context-valid function, variable, event, flow-control, cast, literal, and operator actions for one Blueprint graph snapshot.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "asset_path": _PATH,
                "graph_id": _COMPONENT_ID,
                "expected_snapshot": _SNAPSHOT_ID,
                "text": {"type": "string", "minLength": 1, "maxLength": 128},
                "owner_class": _PATH,
                "function": {"type": "string", "minLength": 1, "maxLength": 128},
                "member": {"type": "string", "minLength": 1, "maxLength": 128},
                "node_family": {
                    "type": "string",
                    "enum": [
                        "function_call", "variable_get", "variable_set", "event",
                        "flow_control", "cast", "literal", "operator",
                    ],
                },
                "pin_context": {
                    "type": "object",
                    "properties": {"node_id": _NODE_ID, "pin_id": _PIN_ID},
                    "required": ["node_id", "pin_id"],
                    "additionalProperties": False,
                },
                "limit": {"type": "integer", "minimum": 1, "maximum": 50},
            },
            "required": ["asset_path", "graph_id", "expected_snapshot"],
            "additionalProperties": False,
        },
    },
    {
        "name": "blueprint_graph_edit",
        "description": "Atomically create, move, remove, configure, or connect Blueprint graph nodes and pins, with opt-in bounded conversion insertion.",
        "inputSchema": {
            "oneOf": [
                _graph_shape(
                    "add_node", ["action_id", "position"],
                    action_id=_ACTION_ID,
                    position=_GRAPH_POSITION,
                ),
                _graph_shape(
                    "move_node", ["node_id", "position"],
                    node_id=_NODE_ID,
                    position=_GRAPH_POSITION,
                ),
                _graph_shape(
                    "remove_node", ["node_id"],
                    node_id=_NODE_ID,
                ),
                _graph_shape(
                    "set_pin_default", ["node_id", "pin_id", "default"],
                    node_id=_NODE_ID,
                    pin_id=_PIN_ID,
                    default=_K2_DEFAULT,
                ),
                _graph_shape(
                    "connect_pins", ["from_node_id", "from_pin_id", "to_node_id", "to_pin_id"],
                    from_node_id=_NODE_ID,
                    from_pin_id=_PIN_ID,
                    to_node_id=_NODE_ID,
                    to_pin_id=_PIN_ID,
                    automatic_conversion={"type": "boolean"},
                ),
                _graph_shape(
                    "disconnect_pins", ["from_node_id", "from_pin_id", "to_node_id", "to_pin_id"],
                    from_node_id=_NODE_ID,
                    from_pin_id=_PIN_ID,
                    to_node_id=_NODE_ID,
                    to_pin_id=_PIN_ID,
                ),
            ]
        },
    },
    {
        "name": "blueprint_create",
        "description": "Reliably create, compile, save, and verify one new supported Blueprint without overwriting content.",
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
        "description": "Reliably compile one current supported Blueprint-family snapshot and return bounded diagnostics.",
        "inputSchema": {
            "type": "object",
            "properties": _mutation_properties(),
            "required": ["operation_id", "asset_path", "expected_snapshot"],
            "additionalProperties": False,
        },
    },
    {
        "name": "blueprint_save",
        "description": "Reliably save one current supported Blueprint-family snapshot non-interactively.",
        "inputSchema": {
            "type": "object",
            "properties": _mutation_properties(),
            "required": ["operation_id", "asset_path", "expected_snapshot"],
            "additionalProperties": False,
        },
    },
    {
        "name": "blueprint_component_edit",
        "description": "Perform one reconciled component hierarchy or component-default edit in a supported Blueprint family.",
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
                _component_shape(
                    "set_replication", ["component_id", "replicates"],
                    component_id=_COMPONENT_ID,
                    replicates={"type": "boolean"},
                ),
            ]
        },
    },
    {
        "name": "blueprint_default_edit",
        "description": "Set one supported editable property on a supported Blueprint family's generated-class default object.",
        "inputSchema": {
            "oneOf": [
                {
                    "type": "object",
                    "properties": _mutation_properties(
                        property_name={"type": "string", "minLength": 1, "maxLength": 128},
                        value=_PROPERTY_VALUE,
                    ),
                    "required": ["operation_id", "asset_path", "expected_snapshot", "property_name", "value"],
                    "additionalProperties": False,
                },
                *[
                    {
                        "type": "object",
                        "properties": _mutation_properties(
                            replication_setting={"const": setting},
                            value=value_schema,
                        ),
                        "required": ["operation_id", "asset_path", "expected_snapshot", "replication_setting", "value"],
                        "additionalProperties": False,
                    }
                    for setting, value_schema in (
                        ("replicates", {"type": "boolean"}),
                        ("replicate_movement", {"type": "boolean"}),
                        ("always_relevant", {"type": "boolean"}),
                        ("only_relevant_to_owner", {"type": "boolean"}),
                        ("use_owner_relevancy", {"type": "boolean"}),
                        ("dormancy", {"type": "string", "enum": ["DORM_Never", "DORM_Awake", "DORM_DormantAll", "DORM_DormantPartial", "DORM_Initial"]}),
                        ("net_priority", {"type": "number", "minimum": 0.01, "maximum": 1000.0}),
                        ("net_update_frequency", {"type": "number", "minimum": 0.01, "maximum": 1000.0}),
                        ("min_net_update_frequency", {"type": "number", "minimum": 0.0, "maximum": 1000.0}),
                    )
                ],
            ]
        },
    },
    {
        "name": "blueprint_member_edit",
        "description": "Safely edit one variable, function, local, macro, or custom-event shell in a supported Blueprint family.",
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
                _scoped_member_shape(
                    "macro", "add", ["name", "signature"],
                    name={"type": "string", "minLength": 1, "maxLength": 128},
                    signature=_MACRO_SIGNATURE,
                    metadata=_MACRO_METADATA,
                ),
                _scoped_member_shape(
                    "macro", "rename", ["macro_id", "new_name"],
                    macro_id=_MACRO_ID,
                    new_name={"type": "string", "minLength": 1, "maxLength": 128},
                ),
                _scoped_member_shape(
                    "macro", "update", ["macro_id", "field", "signature", "policy"],
                    macro_id=_MACRO_ID,
                    field={"const": "signature"},
                    signature=_MACRO_SIGNATURE,
                    policy={"const": "reject_if_referenced"},
                ),
                _scoped_member_shape(
                    "macro", "update", ["macro_id", "field", "metadata"],
                    macro_id=_MACRO_ID,
                    field={"const": "metadata"},
                    metadata=_MACRO_METADATA,
                ),
                _scoped_member_shape(
                    "macro", "remove", ["macro_id", "policy"],
                    macro_id=_MACRO_ID,
                    policy={"const": "reject_if_referenced"},
                ),
                _scoped_member_shape(
                    "custom_event", "add", ["graph_id", "name", "signature"],
                    graph_id=_COMPONENT_ID,
                    name={"type": "string", "minLength": 1, "maxLength": 128},
                    signature=_CUSTOM_EVENT_SIGNATURE,
                    metadata=_CUSTOM_EVENT_METADATA,
                ),
                _scoped_member_shape(
                    "custom_event", "rename", ["custom_event_id", "new_name"],
                    custom_event_id=_CUSTOM_EVENT_ID,
                    new_name={"type": "string", "minLength": 1, "maxLength": 128},
                ),
                _scoped_member_shape(
                    "custom_event", "update", ["custom_event_id", "field", "signature", "policy"],
                    custom_event_id=_CUSTOM_EVENT_ID,
                    field={"const": "signature"},
                    signature=_CUSTOM_EVENT_SIGNATURE,
                    policy={"const": "reject_if_referenced"},
                ),
                _scoped_member_shape(
                    "custom_event", "update", ["custom_event_id", "field", "metadata"],
                    custom_event_id=_CUSTOM_EVENT_ID,
                    field={"const": "metadata"},
                    metadata=_CUSTOM_EVENT_METADATA,
                ),
                _scoped_member_shape(
                    "custom_event", "remove", ["custom_event_id", "policy"],
                    custom_event_id=_CUSTOM_EVENT_ID,
                    policy={"const": "reject_if_referenced"},
                ),
            ]
        },
    },
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
    {
        "name": "game_data_inspect",
        "description": "Inspect one user-defined struct schema or one bounded page of typed Data Table rows from an exact structural snapshot.",
        "inputSchema": {
            "oneOf": [
                {
                    "type": "object",
                    "properties": {
                        "target": {"const": "user_defined_struct"},
                        "asset_path": _PATH,
                        "page_size": {"type": "integer", "minimum": 1, "maximum": 100},
                    },
                    "required": ["target", "asset_path"],
                    "additionalProperties": False,
                },
                {
                    "type": "object",
                    "properties": {
                        "target": {"const": "data_table"},
                        "asset_path": _PATH,
                        "row_names": {
                            "type": "array", "minItems": 1, "maxItems": 64,
                            "items": {"type": "string", "minLength": 1, "maxLength": 128},
                        },
                        "page_size": {"type": "integer", "minimum": 1, "maximum": 100},
                    },
                    "required": ["target", "asset_path"],
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
        "name": "game_data_edit",
        "description": "Create or atomically mutate one bounded user-defined struct schema or typed Data Table row set with compile, save, and snapshot verification.",
        "inputSchema": {
            "oneOf": [
                {
                    "type": "object",
                    "properties": {
                        "operation_id": _OPERATION_ID, "target": {"const": "user_defined_struct"},
                        "operation": {"const": "create"}, "asset_path": _PATH,
                        "members": {"type": "array", "minItems": 1, "maxItems": 64, "items": _STRUCT_MEMBER},
                    },
                    "required": ["operation_id", "target", "operation", "asset_path", "members"],
                    "additionalProperties": False,
                },
                {
                    "type": "object",
                    "properties": {
                        "operation_id": _OPERATION_ID, "target": {"const": "user_defined_struct"},
                        "operation": {"const": "add_member"}, "asset_path": _PATH,
                        "expected_snapshot": _SNAPSHOT_ID, "member": _STRUCT_MEMBER,
                    },
                    "required": ["operation_id", "target", "operation", "asset_path", "expected_snapshot", "member"],
                    "additionalProperties": False,
                },
                {
                    "type": "object",
                    "properties": {
                        "operation_id": _OPERATION_ID, "target": {"const": "user_defined_struct"},
                        "operation": {"const": "rename_member"}, "asset_path": _PATH,
                        "expected_snapshot": _SNAPSHOT_ID, "member_id": _STRUCT_MEMBER_ID,
                        "new_name": {"type": "string", "minLength": 1, "maxLength": 128},
                    },
                    "required": ["operation_id", "target", "operation", "asset_path", "expected_snapshot", "member_id", "new_name"],
                    "additionalProperties": False,
                },
                {
                    "type": "object",
                    "properties": {
                        "operation_id": _OPERATION_ID, "target": {"const": "user_defined_struct"},
                        "operation": {"const": "update_member"}, "asset_path": _PATH,
                        "expected_snapshot": _SNAPSHOT_ID, "member_id": _STRUCT_MEMBER_ID,
                        "field": {"const": "type"}, "type": _K2_TYPE,
                        "policy": {"const": "reject_if_referenced"},
                    },
                    "required": ["operation_id", "target", "operation", "asset_path", "expected_snapshot", "member_id", "field", "type", "policy"],
                    "additionalProperties": False,
                },
                {
                    "type": "object",
                    "properties": {
                        "operation_id": _OPERATION_ID, "target": {"const": "user_defined_struct"},
                        "operation": {"const": "update_member"}, "asset_path": _PATH,
                        "expected_snapshot": _SNAPSHOT_ID, "member_id": _STRUCT_MEMBER_ID,
                        "field": {"const": "default"}, "default": _K2_DEFAULT,
                    },
                    "required": ["operation_id", "target", "operation", "asset_path", "expected_snapshot", "member_id", "field", "default"],
                    "additionalProperties": False,
                },
                {
                    "type": "object",
                    "properties": {
                        "operation_id": _OPERATION_ID, "target": {"const": "user_defined_struct"},
                        "operation": {"const": "reorder_member"}, "asset_path": _PATH,
                        "expected_snapshot": _SNAPSHOT_ID, "member_id": _STRUCT_MEMBER_ID,
                        "relative_to_member_id": _STRUCT_MEMBER_ID,
                        "position": {"type": "string", "enum": ["above", "below"]},
                    },
                    "required": ["operation_id", "target", "operation", "asset_path", "expected_snapshot", "member_id", "relative_to_member_id", "position"],
                    "additionalProperties": False,
                },
                {
                    "type": "object",
                    "properties": {
                        "operation_id": _OPERATION_ID, "target": {"const": "user_defined_struct"},
                        "operation": {"const": "remove_member"}, "asset_path": _PATH,
                        "expected_snapshot": _SNAPSHOT_ID, "member_id": _STRUCT_MEMBER_ID,
                        "policy": {"const": "reject_if_referenced"},
                    },
                    "required": ["operation_id", "target", "operation", "asset_path", "expected_snapshot", "member_id", "policy"],
                    "additionalProperties": False,
                },
                {
                    "type": "object",
                    "properties": {
                        "operation_id": _OPERATION_ID, "target": {"const": "data_table"},
                        "operation": {"const": "create"}, "asset_path": _PATH, "row_struct": _PATH,
                        "rows": {"type": "array", "maxItems": 64, "items": _ROW_WRITE},
                    },
                    "required": ["operation_id", "target", "operation", "asset_path", "row_struct"],
                    "additionalProperties": False,
                },
                *[
                    {
                        "type": "object",
                        "properties": {
                            "operation_id": _OPERATION_ID, "target": {"const": "data_table"},
                            "operation": {"const": operation}, "asset_path": _PATH,
                            "expected_snapshot": _SNAPSHOT_ID,
                            "row_name": {"type": "string", "minLength": 1, "maxLength": 128},
                            "values": _ROW_VALUES,
                            **({"preserve_unspecified": {"type": "boolean"}} if operation == "replace_row" else {}),
                        },
                        "required": ["operation_id", "target", "operation", "asset_path", "expected_snapshot", "row_name", "values"],
                        "additionalProperties": False,
                    }
                    for operation in ("add_row", "replace_row")
                ],
                {
                    "type": "object",
                    "properties": {
                        "operation_id": _OPERATION_ID, "target": {"const": "data_table"},
                        "operation": {"const": "rename_row"}, "asset_path": _PATH,
                        "expected_snapshot": _SNAPSHOT_ID,
                        "row_name": {"type": "string", "minLength": 1, "maxLength": 128},
                        "new_row_name": {"type": "string", "minLength": 1, "maxLength": 128},
                    },
                    "required": ["operation_id", "target", "operation", "asset_path", "expected_snapshot", "row_name", "new_row_name"],
                    "additionalProperties": False,
                },
                {
                    "type": "object",
                    "properties": {
                        "operation_id": _OPERATION_ID, "target": {"const": "data_table"},
                        "operation": {"const": "remove_row"}, "asset_path": _PATH,
                        "expected_snapshot": _SNAPSHOT_ID,
                        "row_name": {"type": "string", "minLength": 1, "maxLength": 128},
                    },
                    "required": ["operation_id", "target", "operation", "asset_path", "expected_snapshot", "row_name"],
                    "additionalProperties": False,
                },
                {
                    "type": "object",
                    "properties": {
                        "operation_id": _OPERATION_ID, "target": {"const": "data_table"},
                        "operation": {"const": "batch"}, "asset_path": _PATH,
                        "expected_snapshot": _SNAPSHOT_ID,
                        "upserts": {"type": "array", "maxItems": 64, "items": _ROW_WRITE},
                        "remove_rows": {
                            "type": "array", "maxItems": 64,
                            "items": {"type": "string", "minLength": 1, "maxLength": 128},
                        },
                    },
                    "required": ["operation_id", "target", "operation", "asset_path", "expected_snapshot", "upserts", "remove_rows"],
                    "additionalProperties": False,
                },
            ]
        },
    },
)
TOOL_BY_NAME: Final = {tool["name"]: tool for tool in TOOLS}
