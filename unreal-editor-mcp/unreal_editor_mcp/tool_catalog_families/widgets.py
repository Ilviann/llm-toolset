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
            "Perform one stale-safe structural Widget Blueprint tree or safe widget-default "
            "mutation using stable widget and slot identities."
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
            ]
        },
    },
)
