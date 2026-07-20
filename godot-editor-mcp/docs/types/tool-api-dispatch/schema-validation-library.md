# Library: published-schema validation

**Source:** `godot_editor_mcp/schema_validation.py`

`validate_tool_arguments(value, schema)` enforces exactly the dependency-free JSON Schema subset published by the catalog: local `$ref`, exact `oneOf`, required fields, JSON scalar types, enums/constants, numeric/string/array/object bounds, patterns, nested items/properties, and `additionalProperties`.

`SchemaValidationError` reports a model-facing field path and reason. Python booleans are not accepted as integers/numbers. Non-finite numbers fail. Equality follows JSON scalar distinctions. Add a keyword only when schemas publish it, implement it recursively, and table-test normal and failing nested cases.
