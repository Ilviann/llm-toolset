import math
import unittest

from unreal_editor_mcp.schema_validation import (
    MAX_SCHEMA_ERROR_BYTES, SchemaValidationError, validate_tool_arguments,
)


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

    def test_validates_typed_additional_properties_and_property_names(self):
        schema = {
            "type": "object",
            "maxProperties": 2,
            "propertyNames": {"type": "string", "pattern": "^[A-Z][A-Za-z]*$"},
            "additionalProperties": {"type": "integer", "minimum": 0},
        }
        validate_tool_arguments({"Damage": 42}, schema)
        for value in ({"damage": 42}, {"Damage": -1}, {"Damage": 1.5}):
            with self.subTest(value=value), self.assertRaises(SchemaValidationError):
                validate_tool_arguments(value, schema)

    def test_one_of_reports_each_shape_failure_and_expected_limits(self):
        schema = {"oneOf": [
            {"type": "object", "properties": {"path": {"type": "string"}},
             "required": ["path"], "additionalProperties": False},
            {"type": "object", "properties": {
                "cursor": {"type": "string"}, "page_size": {"type": "integer", "maximum": 100},
            }, "required": ["cursor"], "additionalProperties": False},
        ]}
        cases = (
            ({}, "missing required field 'path'", "missing required field 'cursor'"),
            ({"path": 1}, "arguments.path must be string", "missing required field 'cursor'"),
            ({"cursor": "ok", "page_size": 101}, "missing required field 'path'",
             "arguments.page_size exceeds the maximum 100"),
            ({"cursor": "ok", "path": "ok"}, "unknown field 'cursor'", "unknown field 'path'"),
        )
        for value, first, second in cases:
            with self.subTest(value=value), self.assertRaises(SchemaValidationError) as caught:
                validate_tool_arguments(value, schema)
            message = str(caught.exception)
            self.assertIn("no shapes matched", message)
            self.assertIn(first, message)
            self.assertIn(second, message)

    def test_matching_discriminator_failure_is_first_even_for_late_shape(self):
        schema = {"oneOf": [
            {"type": "object", "properties": {
                "operation": {"const": str(index)}, "name": {"type": "string"},
            }, "required": ["operation", "name"], "additionalProperties": False}
            for index in range(12)
        ]}
        with self.assertRaises(SchemaValidationError) as caught:
            validate_tool_arguments({"operation": "11", "name": 42}, schema)
        message = str(caught.exception)
        self.assertIn("shape 12: arguments.name must be string", message)
        self.assertLess(message.index("shape 12:"), message.index("shape 1:"))
        self.assertIn("8 other shapes omitted", message)
        validate_tool_arguments({"operation": "11", "name": "ok"}, schema)

    def test_one_of_nested_reference_failure_preserves_field_and_array_index(self):
        schema = {
            "type": "array", "items": {"oneOf": [{"$ref": "#/$defs/item"}, {"type": "string"}]},
            "$defs": {"item": {"type": "object", "properties": {
                "value": {"oneOf": [{"type": "boolean"}, {"type": "number", "minimum": 0}]},
            }}},
        }
        with self.assertRaises(SchemaValidationError) as caught:
            validate_tool_arguments([{"value": -1}], schema)
        message = str(caught.exception)
        self.assertIn("arguments[0].value must be boolean", message)
        self.assertIn("arguments[0].value is below the minimum 0", message)
        validate_tool_arguments([{"value": True}], schema)

    def test_one_of_ambiguity_is_distinct_and_bounded(self):
        schema = {"oneOf": [{"type": "number"}] * 12}
        with self.assertRaises(SchemaValidationError) as caught:
            validate_tool_arguments(2, schema)
        message = str(caught.exception)
        self.assertIn("12 shapes matched (shapes 1, 2, 3, 4, 8 more)", message)
        self.assertIn("ambiguous", message)
        self.assertNotIn("no shapes matched", message)

    def test_error_output_bounds_unicode_and_escapes_untrusted_keys(self):
        unknown = "\U0001f600" * 10000
        schema = {"type": "object", "additionalProperties": False}
        for candidate in (schema, {"oneOf": [schema] * 40}):
            with self.subTest(candidate=candidate), self.assertRaises(SchemaValidationError) as caught:
                validate_tool_arguments({unknown: "private value"}, candidate)
            message = str(caught.exception)
            self.assertLessEqual(len(message.encode("utf-8")), MAX_SCHEMA_ERROR_BYTES)
            self.assertIn("...", message)
            self.assertNotIn("private value", message)
        typed = {"type": "object", "additionalProperties": {"type": "integer"}}
        with self.assertRaises(SchemaValidationError) as caught:
            validate_tool_arguments({"line\nbreak": "private value"}, typed)
        self.assertIn("arguments['line\\nbreak'] must be integer", str(caught.exception))
        self.assertNotIn("\n", str(caught.exception))

    def test_malformed_one_of_schemas_are_still_internal_errors(self):
        for alternatives in ("bad", [42]):
            with self.subTest(alternatives=alternatives), self.assertRaises(RuntimeError):
                validate_tool_arguments({}, {"oneOf": alternatives})
