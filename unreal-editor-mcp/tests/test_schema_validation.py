import math
import unittest

from unreal_editor_mcp.schema_validation import SchemaValidationError, validate_tool_arguments


class SchemaValidationTests(unittest.TestCase):
    def test_rejects_unknown_and_missing_fields(self):
        schema = {
            "type": "object",
            "properties": {"name": {"type": "string", "maxLength": 4}},
            "required": ["name"],
            "additionalProperties": False,
        }
        with self.assertRaises(SchemaValidationError):
            validate_tool_arguments({}, schema)
        with self.assertRaises(SchemaValidationError):
            validate_tool_arguments({"name": "ok", "extra": 1}, schema)

    def test_bounds_strings_arrays_and_numbers(self):
        schema = {
            "type": "object",
            "properties": {
                "items": {"type": "array", "maxItems": 1, "items": {"type": "integer"}},
                "number": {"type": "number", "minimum": 0, "maximum": 1},
            },
            "additionalProperties": False,
        }
        for value in (
            {"items": [1, 2]}, {"items": [True]}, {"number": -1}, {"number": math.inf}
        ):
            with self.subTest(value=value), self.assertRaises(SchemaValidationError):
                validate_tool_arguments(value, schema)

    def test_resolves_local_reference_and_one_of(self):
        schema = {
            "oneOf": [{"$ref": "#/$defs/a"}, {"type": "integer"}],
            "$defs": {"a": {"type": "string", "pattern": "^a+$"}},
        }
        validate_tool_arguments("aa", schema)
        validate_tool_arguments(2, schema)
        with self.assertRaises(SchemaValidationError):
            validate_tool_arguments("b", schema)
