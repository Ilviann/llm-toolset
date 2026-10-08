"""Dependency-free validation for the JSON Schema subset used by tools."""

from __future__ import annotations

import math
import re
from collections.abc import Mapping, Sequence
from typing import Any


MAX_SCHEMA_ERROR_BYTES = 4096
_MAX_SHAPE_DETAILS = 4
_MAX_SHAPE_ERROR_BYTES = 768


def _bounded_text(message: str, limit: int) -> str:
    encoded = message.encode("utf-8", errors="backslashreplace")
    if len(encoded) <= limit:
        return encoded.decode("utf-8")
    return encoded[:limit - 3].decode("utf-8", errors="ignore") + "..."


class SchemaValidationError(ValueError):
    def __init__(self, message: str) -> None:
        super().__init__(_bounded_text(message, MAX_SCHEMA_ERROR_BYTES))


def _shape_priority(value: Any, schema: Mapping[str, Any]) -> tuple[int, int, int]:
    """Prefer supplied discriminators and familiar fields for diagnostics only."""
    if not isinstance(value, Mapping):
        return (0, 0, 0)
    properties = schema.get("properties", {})
    if not isinstance(properties, Mapping):
        return (0, 0, 0)
    matches = mismatches = 0
    for name, child in properties.items():
        if name in value and isinstance(child, Mapping) and "const" in child:
            if _json_equal(value[name], child["const"]):
                matches += 1
            else:
                mismatches += 1
    unknown = sum(name not in properties for name in value)
    return (-matches, mismatches, unknown)


def validate_tool_arguments(value: Any, schema: Mapping[str, Any]) -> None:
    _validate(value, schema, schema, "arguments")


def _validate(value: Any, schema: Mapping[str, Any], root: Mapping[str, Any], path: str) -> None:
    reference = schema.get("$ref")
    if reference is not None:
        if not isinstance(reference, str) or not reference.startswith("#/"):
            raise RuntimeError(f"Unsupported schema reference: {reference!r}")
        resolved: Any = root
        for segment in reference[2:].split("/"):
            key = segment.replace("~1", "/").replace("~0", "~")
            if not isinstance(resolved, Mapping) or key not in resolved:
                raise RuntimeError(f"Unresolved schema reference: {reference}")
            resolved = resolved[key]
        if not isinstance(resolved, Mapping):
            raise RuntimeError(f"Schema reference is not an object: {reference}")
        _validate(value, resolved, root, path)
        return

    alternatives = schema.get("oneOf")
    if alternatives is not None:
        if not isinstance(alternatives, Sequence) or isinstance(alternatives, str):
            raise RuntimeError("oneOf must contain schemas")
        matches = 0
        matching_shapes: list[int] = []
        failures: list[tuple[tuple[int, int, int], int, str]] = []
        for index, alternative in enumerate(alternatives, start=1):
            if not isinstance(alternative, Mapping):
                raise RuntimeError("oneOf entries must be schemas")
            try:
                _validate(value, alternative, root, path)
            except SchemaValidationError as exc:
                failures.append((
                    _shape_priority(value, alternative), index,
                    _bounded_text(str(exc), _MAX_SHAPE_ERROR_BYTES),
                ))
                failures.sort()
                del failures[_MAX_SHAPE_DETAILS:]
                continue
            matches += 1
            if len(matching_shapes) < _MAX_SHAPE_DETAILS:
                matching_shapes.append(index)
        if matches != 1:
            message = f"{path} must match exactly one allowed shape"
            if matches:
                message += f"; {matches} shapes matched (shapes {', '.join(map(str, matching_shapes))}"
                if matches > len(matching_shapes):
                    message += f", {matches - len(matching_shapes)} more"
                message += "). The request is ambiguous."
            else:
                message += "; no shapes matched. Closest failures (first error per shape): "
                message += "; ".join(f"shape {index}: {reason}" for _, index, reason in failures)
                omitted = len(alternatives) - len(failures)
                if omitted:
                    message += f"; {omitted} other shapes omitted"
            raise SchemaValidationError(message)

    expected_type = schema.get("type")
    if expected_type is not None and not _matches_type(value, expected_type):
        raise SchemaValidationError(f"{path} must be {expected_type}")
    if "const" in schema and not _json_equal(value, schema["const"]):
        raise SchemaValidationError(f"{path} must equal {schema['const']!r}")
    if "enum" in schema and not any(_json_equal(value, item) for item in schema["enum"]):
        raise SchemaValidationError(f"{path} must be one of {schema['enum']!r}")

    if isinstance(value, str):
        if len(value) < int(schema.get("minLength", 0)):
            raise SchemaValidationError(f"{path} is shorter than the minimum length {schema['minLength']}")
        if "maxLength" in schema and len(value) > int(schema["maxLength"]):
            raise SchemaValidationError(f"{path} exceeds the maximum length {schema['maxLength']}")
        if "pattern" in schema and re.search(str(schema["pattern"]), value) is None:
            raise SchemaValidationError(f"{path} does not match the required pattern {schema['pattern']!r}")
    elif isinstance(value, Mapping):
        _validate_object(value, schema, root, path)
    elif isinstance(value, Sequence) and not isinstance(value, (str, bytes, bytearray)):
        _validate_array(value, schema, root, path)
    elif _is_number(value):
        if isinstance(value, float) and not math.isfinite(value):
            raise SchemaValidationError(f"{path} must be finite")
        if "minimum" in schema and value < schema["minimum"]:
            raise SchemaValidationError(f"{path} is below the minimum {schema['minimum']}")
        if "maximum" in schema and value > schema["maximum"]:
            raise SchemaValidationError(f"{path} exceeds the maximum {schema['maximum']}")


def _validate_object(
    value: Mapping[Any, Any], schema: Mapping[str, Any], root: Mapping[str, Any], path: str
) -> None:
    if not all(isinstance(key, str) for key in value):
        raise SchemaValidationError(f"{path} property names must be strings")
    if "maxProperties" in schema and len(value) > int(schema["maxProperties"]):
        raise SchemaValidationError(f"{path} has too many properties (maximum {schema['maxProperties']})")
    if len(value) < int(schema.get("minProperties", 0)):
        raise SchemaValidationError(f"{path} has too few properties (minimum {schema['minProperties']})")
    required = schema.get("required", ())
    missing = [name for name in required if name not in value]
    if missing:
        raise SchemaValidationError(f"{path} is missing required field {missing[0]!r}")
    properties = schema.get("properties", {})
    if not isinstance(properties, Mapping):
        raise RuntimeError("properties must be an object")
    if schema.get("additionalProperties") is False:
        unknown = [name for name in value if name not in properties]
        if unknown:
            raise SchemaValidationError(f"{path} has unknown field {unknown[0]!r}")
    property_names = schema.get("propertyNames")
    if isinstance(property_names, Mapping):
        for name in value:
            _validate(name, property_names, root, f"{path} property name")
    for name, item in value.items():
        child = properties.get(name)
        if isinstance(child, Mapping):
            _validate(item, child, root, f"{path}.{name}")
        elif isinstance(schema.get("additionalProperties"), Mapping):
            _validate(item, schema["additionalProperties"], root, f"{path}[{name!r}]")


def _validate_array(
    value: Sequence[Any], schema: Mapping[str, Any], root: Mapping[str, Any], path: str
) -> None:
    if len(value) < int(schema.get("minItems", 0)):
        raise SchemaValidationError(f"{path} has too few items (minimum {schema['minItems']})")
    if "maxItems" in schema and len(value) > int(schema["maxItems"]):
        raise SchemaValidationError(f"{path} has too many items (maximum {schema['maxItems']})")
    items = schema.get("items")
    if isinstance(items, Mapping):
        for index, item in enumerate(value):
            _validate(item, items, root, f"{path}[{index}]")


def _matches_type(value: Any, expected: Any) -> bool:
    if expected == "object":
        return isinstance(value, Mapping)
    if expected == "array":
        return isinstance(value, Sequence) and not isinstance(value, (str, bytes, bytearray))
    if expected == "string":
        return isinstance(value, str)
    if expected == "integer":
        return isinstance(value, int) and not isinstance(value, bool)
    if expected == "number":
        return _is_number(value)
    if expected == "boolean":
        return isinstance(value, bool)
    if expected == "null":
        return value is None
    raise RuntimeError(f"Unsupported schema type: {expected!r}")


def _is_number(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def _json_equal(left: Any, right: Any) -> bool:
    return left == right if _is_number(left) and _is_number(right) else type(left) is type(right) and left == right
