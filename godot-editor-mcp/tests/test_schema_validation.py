from __future__ import annotations

import unittest

from godot_editor_mcp.schema_validation import (
    SchemaValidationError,
    validate_tool_arguments,
)
from godot_editor_mcp.tool_catalog import SPEC_BY_NAME


class SchemaValidationTests(unittest.TestCase):
    def assert_invalid(self, value, schema, message: str | None = None) -> None:
        with self.assertRaises(SchemaValidationError) as caught:
            validate_tool_arguments(value, schema)
        if message is not None:
            self.assertIn(message, str(caught.exception))

    def test_supported_scalar_keywords_are_enforced(self) -> None:
        cases = [
            ("type", True, {"type": "integer"}),
            ("enum", "c", {"enum": ["a", "b"]}),
            ("const", "b", {"const": "a"}),
            ("minimum", 0, {"type": "integer", "minimum": 1}),
            ("maximum", 2, {"type": "number", "maximum": 1}),
            ("minLength", "", {"type": "string", "minLength": 1}),
            ("maxLength", "long", {"type": "string", "maxLength": 3}),
            ("pattern", "user://x", {"type": "string", "pattern": "^res://"}),
        ]
        for keyword, value, schema in cases:
            with self.subTest(keyword=keyword):
                self.assert_invalid(value, schema)

    def test_supported_object_and_array_keywords_are_enforced(self) -> None:
        schema = {
            "type": "object",
            "properties": {
                "items": {
                    "type": "array", "minItems": 1, "maxItems": 2,
                    "items": {"type": "string"},
                },
                "record": {
                    "type": "object", "minProperties": 1, "maxProperties": 1,
                    "properties": {"name": {"type": "string"}},
                    "additionalProperties": False,
                },
            },
            "required": ["items", "record"],
            "additionalProperties": False,
        }
        validate_tool_arguments({"items": ["a"], "record": {"name": "ok"}}, schema)
        for value in (
            {},
            {"items": [], "record": {"name": "ok"}},
            {"items": ["a", "b", "c"], "record": {"name": "ok"}},
            {"items": [1], "record": {"name": "ok"}},
            {"items": ["a"], "record": {}},
            {"items": ["a"], "record": {"name": "ok", "extra": True}},
            {"items": ["a"], "record": {"name": "ok"}, "unknown": True},
        ):
            with self.subTest(value=value):
                self.assert_invalid(value, schema)

    def test_one_of_and_local_references_are_enforced(self) -> None:
        schema = {
            "oneOf": [{"$ref": "#/$defs/a"}, {"$ref": "#/$defs/b"}],
            "$defs": {
                "a": {"type": "object", "properties": {"kind": {"const": "a"}}, "required": ["kind"]},
                "b": {"type": "object", "properties": {"kind": {"const": "b"}}, "required": ["kind"]},
            },
        }
        validate_tool_arguments({"kind": "a"}, schema)
        self.assert_invalid({"kind": "c"}, schema, "exactly one")

    def test_nested_scene_transaction_shapes_and_untyped_values(self) -> None:
        schema = SPEC_BY_NAME["scene_transaction"].inputSchema
        valid = {
            "operations": [
                {"op": "add_node", "parent": {"path": "."}, "type": "Node2D", "name": "Hero", "handle": "hero"},
                {"op": "set_property", "target": {"handle": "hero"}, "property": "metadata/value", "value": {"nested": [1, True, None]}},
            ]
        }
        validate_tool_arguments(valid, schema)
        invalid = {"operations": [{"op": "remove_node", "target": {"path": "Hero", "handle": "hero"}}]}
        self.assert_invalid(invalid, schema)

    def test_recursive_create_scene_reference_is_resolved(self) -> None:
        schema = SPEC_BY_NAME["create_scene"].inputSchema
        validate_tool_arguments({
            "path": "scenes/main.tscn", "root_type": "Node", "root_name": "Main",
            "children": [{"type": "Node2D", "name": "Child", "children": [{"type": "Node", "name": "Leaf"}]}],
        }, schema)
        self.assert_invalid({
            "path": "scenes/main.tscn", "root_type": "Node", "root_name": "Main",
            "children": [{"type": "Node2D", "name": "Child", "unknown": True}],
        }, schema)


if __name__ == "__main__":
    unittest.main()
