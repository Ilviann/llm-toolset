"""User-defined struct and typed Data Table tools."""

from __future__ import annotations

from typing import Final

from .schemas import (
    _K2_DEFAULT,
    _K2_TYPE,
    _OPERATION_ID,
    _PATH,
    _SNAPSHOT_ID,
    _STRUCT_MEMBER_ID,
)

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

GAME_DATA_TOOLS: Final = (
    {
        "name": "game_data_inspect",
        "description": "Inspect one user-defined struct, typed Data Table, or Data Asset through bounded reflected values from an exact structural snapshot.",
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
                        "target": {"const": "data_asset"},
                        "asset_path": _PATH,
                        "property_names": {
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
