"""Widget Blueprint hierarchy authoring tool."""

from __future__ import annotations

from typing import Final

from .schemas import (
    _COMPONENT_ID,
    _OPERATION_ID,
    _PATH,
    _PROPERTY_VALUE,
    _SNAPSHOT_ID,
)


_NAME: Final = {"type": "string", "minLength": 1, "maxLength": 128}


def _widget_value(depth: int = 4) -> dict[str, object]:
    scalar: list[dict[str, object]] = [
        {"type": "boolean"},
        {"type": "number"},
        {"type": "string", "maxLength": 4096},
        {
            "type": "object",
            "properties": {
                "kind": {"const": "reference"},
                "path": {
                    "type": "string",
                    "maxLength": 512,
                    "pattern": r"^(|/(?!.*\\.\\.)[^\\\\]+)$",
                },
            },
            "required": ["kind", "path"],
            "additionalProperties": False,
        },
    ]
    if depth <= 0:
        return {"oneOf": scalar}
    child = _widget_value(depth - 1)
    return {
        "oneOf": [
            *scalar,
            {"type": "array", "maxItems": 64, "items": child},
            {
                "type": "object",
                "properties": {
                    "kind": {"const": "struct"},
                    "fields": {
                        "type": "object",
                        "maxProperties": 64,
                        "propertyNames": {
                            "type": "string", "minLength": 1, "maxLength": 128,
                        },
                        "additionalProperties": child,
                    },
                },
                "required": ["kind", "fields"],
                "additionalProperties": False,
            },
        ]
    }


_WIDGET_VALUE: Final = _widget_value()
_TARGET: Final = {
    "oneOf": [
        {
            "type": "object",
            "properties": {
                "kind": {"const": "panel"},
                "parent_id": _COMPONENT_ID,
                "index": {"type": "integer", "minimum": 0, "maximum": 511},
            },
            "required": ["kind", "parent_id"],
            "additionalProperties": False,
        },
        {
            "type": "object",
            "properties": {
                "kind": {"const": "named_slot"},
                "slot_id": _COMPONENT_ID,
            },
            "required": ["kind", "slot_id"],
            "additionalProperties": False,
        },
    ]
}


def _shape(operation: str, required: list[str], **properties: object) -> dict[str, object]:
    return {
        "type": "object",
        "properties": {
            "operation_id": _OPERATION_ID,
            "asset_path": _PATH,
            "expected_snapshot": _SNAPSHOT_ID,
            "operation": {"const": operation},
            **properties,
        },
        "required": [
            "operation_id", "asset_path", "expected_snapshot", "operation", *required,
        ],
        "additionalProperties": False,
    }


WIDGET_TOOLS: Final = (
    {
        "name": "widget_tree_edit",
        "description": (
            "Perform one stale-safe Widget Blueprint hierarchy, layout, presentation, "
            "property-binding, or designer-event mutation using stable identities."
        ),
        "inputSchema": {
            "oneOf": [
                _shape(
                    "set_root",
                    ["widget_class", "name"],
                    widget_class=_PATH,
                    name=_NAME,
                ),
                _shape(
                    "add",
                    ["widget_class", "name", "target"],
                    widget_class=_PATH,
                    name=_NAME,
                    target=_TARGET,
                ),
                _shape(
                    "remove",
                    ["widget_id", "policy"],
                    widget_id=_COMPONENT_ID,
                    policy={"const": "reject_if_referenced"},
                ),
                _shape(
                    "rename",
                    ["widget_id", "new_name"],
                    widget_id=_COMPONENT_ID,
                    new_name=_NAME,
                ),
                _shape(
                    "reparent",
                    ["widget_id", "target"],
                    widget_id=_COMPONENT_ID,
                    target=_TARGET,
                ),
                _shape(
                    "set_variable",
                    ["widget_id", "is_variable"],
                    widget_id=_COMPONENT_ID,
                    is_variable={"type": "boolean"},
                ),
                _shape(
                    "set_property",
                    ["widget_id", "property_name", "value"],
                    widget_id=_COMPONENT_ID,
                    property_name=_NAME,
                    value=_PROPERTY_VALUE,
                ),
                _shape(
                    "set_slot",
                    ["slot_id", "property_name", "value"],
                    slot_id=_COMPONENT_ID,
                    property_name=_NAME,
                    value=_WIDGET_VALUE,
                ),
                _shape(
                    "set_style",
                    ["widget_id", "property_name", "value"],
                    widget_id=_COMPONENT_ID,
                    property_name=_NAME,
                    value=_WIDGET_VALUE,
                ),
                _shape(
                    "bind_property",
                    ["widget_id", "target_property", "source_kind", "source_name"],
                    widget_id=_COMPONENT_ID,
                    target_property=_NAME,
                    source_kind={"type": "string", "enum": ["function", "property"]},
                    source_name=_NAME,
                ),
                _shape(
                    "unbind_property",
                    ["widget_id", "target_property"],
                    widget_id=_COMPONENT_ID,
                    target_property=_NAME,
                ),
                _shape(
                    "bind_event",
                    ["widget_id", "delegate_name"],
                    widget_id=_COMPONENT_ID,
                    delegate_name=_NAME,
                ),
                _shape(
                    "unbind_event",
                    ["widget_id", "delegate_name", "policy"],
                    widget_id=_COMPONENT_ID,
                    delegate_name=_NAME,
                    policy={"const": "reject_if_connected"},
                ),
            ]
        },
    },
)
