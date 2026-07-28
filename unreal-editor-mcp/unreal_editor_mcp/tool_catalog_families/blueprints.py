"""Blueprint inspection and authoring tools."""

from __future__ import annotations

from typing import Final

from .schemas import (
    _ACTION_ID,
    _COMPONENT_ID,
    _CUSTOM_EVENT_ID,
    _CUSTOM_EVENT_METADATA,
    _CUSTOM_EVENT_SIGNATURE,
    _FUNCTION_ID,
    _FUNCTION_METADATA,
    _FUNCTION_SIGNATURE,
    _GRAPH_POSITION,
    _K2_DEFAULT,
    _K2_TYPE,
    _LOCAL_ID,
    _MACRO_ID,
    _MACRO_METADATA,
    _MACRO_SIGNATURE,
    _MEMBER_ID,
    _MEMBER_METADATA,
    _NODE_ID,
    _OPERATION_ID,
    _PATH,
    _PIN_ID,
    _PROPERTY_VALUE,
    _SNAPSHOT_ID,
    _component_shape,
    _graph_shape,
    _member_shape,
    _mutation_properties,
    _scoped_member_shape,
)

BLUEPRINT_TOOLS: Final = (
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
                            "maxItems": 17,
                            "items": {
                                "type": "string",
                                "enum": [
                                    "summary", "parent_class", "compile_state", "components",
                                    "class_defaults", "variables", "functions", "macros", "custom_events",
                                    "parameters", "local_variables",
                                    "graphs", "nodes", "pins", "connections",
                                    "widget_tree", "widget_defaults",
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
                        "widget_id": _COMPONENT_ID,
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
)
