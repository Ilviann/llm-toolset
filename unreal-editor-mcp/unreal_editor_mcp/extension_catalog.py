"""Exact-version companion extension schemas and native capability intersection."""

from __future__ import annotations

from copy import deepcopy
from typing import Final


COMPANION_API_VERSION: Final = 1
EXTENSION_SCHEMA_REVISION: Final = 1

_PATH: Final = {
    "type": "string", "minLength": 3, "maxLength": 512,
    "pattern": r"^(?!.*\.\.)(?!.*\\)/[^\s]+$",
}
_OPERATION_ID: Final = {"type": "string", "pattern": r"^[0-9a-f]{32}$"}
_SNAPSHOT: Final = {"type": "string", "pattern": r"^[0-9a-f]{40}$"}


def _read_shape(operation: str) -> dict[str, object]:
    return {
        "type": "object",
        "properties": {
            "extension_id": {"const": "unreal-mcp-test"},
            "extension_schema_revision": {"const": EXTENSION_SCHEMA_REVISION},
            "operation": {"const": operation},
            "asset_path": _PATH,
        },
        "required": ["extension_id", "extension_schema_revision", "operation", "asset_path"],
        "additionalProperties": False,
    }


def _mutation_shape(operation: str) -> dict[str, object]:
    return {
        "type": "object",
        "properties": {
            "extension_id": {"const": "unreal-mcp-test"},
            "extension_schema_revision": {"const": EXTENSION_SCHEMA_REVISION},
            "operation": {"const": operation},
            "operation_id": _OPERATION_ID,
            "asset_path": _PATH,
            "expected_snapshot": _SNAPSHOT,
            "value": {"type": "integer", "minimum": -1000000, "maximum": 1000000},
        },
        "required": [
            "extension_id", "extension_schema_revision", "operation", "operation_id",
            "asset_path", "expected_snapshot", "value",
        ],
        "additionalProperties": False,
    }


EXTENSION_CATALOG: Final = {
    "unreal-mcp-test": {
        "schema_revision": EXTENSION_SCHEMA_REVISION,
        "contributions": {
            ("blueprint_inspect", "inspect_test_asset", "read"):
                _read_shape("inspect_test_asset"),
            ("blueprint_default_edit", "set_test_asset_value", "mutation"):
                _mutation_shape("set_test_asset_value"),
            ("blueprint_inspect", "inspect_test_component", "read"):
                _read_shape("inspect_test_component"),
            ("blueprint_component_edit", "set_test_component_value", "mutation"):
                _mutation_shape("set_test_component_value"),
            ("blueprint_inspect", "inspect_test_contribution", "read"):
                _read_shape("inspect_test_contribution"),
            ("blueprint_default_edit", "set_test_contribution_value", "mutation"):
                _mutation_shape("set_test_contribution_value"),
        },
    },
    "unreal-mcp-gas": {
        "schema_revision": EXTENSION_SCHEMA_REVISION,
        "contributions": {},
        "integrated_sections": {
            ("blueprint_inspect", "inspect_gameplay_ability", "read"):
                ("gameplay_ability",),
        },
    },
}


def compose_extension_tools(
    base_tools: tuple[dict[str, object], ...],
    native_capabilities: object,
    *,
    writable: bool,
) -> tuple[dict[str, object], ...]:
    """Return base tools plus only exact Python/native companion schema matches."""
    tools = deepcopy(base_tools)
    if not isinstance(native_capabilities, dict):
        return tools
    if native_capabilities.get("companion_api_version") != COMPANION_API_VERSION:
        return tools
    companions = native_capabilities.get("companions")
    if not isinstance(companions, list):
        return tools
    accepted: list[tuple[str, str, str, dict[str, object]]] = []
    integrated_sections: dict[str, set[str]] = {}
    for companion in companions[:64]:
        if not isinstance(companion, dict) or companion.get("ready") is not True:
            continue
        extension_id = companion.get("extension_id")
        known = EXTENSION_CATALOG.get(extension_id) if isinstance(extension_id, str) else None
        if (
            known is None
            or companion.get("companion_api_version") != COMPANION_API_VERSION
            or companion.get("schema_revision") != known["schema_revision"]
        ):
            continue
        native_contributions = companion.get("contributions")
        if not isinstance(native_contributions, list) or len(native_contributions) > 32:
            continue
        native_keys = {
            (item.get("tool_family"), item.get("operation"), item.get("access"))
            for item in native_contributions if isinstance(item, dict)
        }
        for key, schema in known["contributions"].items():
            if key in native_keys and (key[2] == "read" or writable):
                accepted.append((*key, schema))
        for key, sections in known.get("integrated_sections", {}).items():
            if key in native_keys and (key[2] == "read" or writable):
                integrated_sections.setdefault(key[0], set()).update(sections)

    by_name = {tool["name"]: tool for tool in tools}
    for tool_name, _operation, _access, schema in sorted(accepted):
        tool = by_name.get(tool_name)
        if tool is None:
            continue
        input_schema = tool["inputSchema"]
        if not isinstance(input_schema, dict):
            continue
        if isinstance(input_schema.get("oneOf"), list):
            input_schema["oneOf"].append(deepcopy(schema))
        else:
            tool["inputSchema"] = {"oneOf": [deepcopy(input_schema), deepcopy(schema)]}
    for tool_name, sections in integrated_sections.items():
        tool = by_name.get(tool_name)
        if tool is None or not isinstance(tool.get("inputSchema"), dict):
            continue
        branches = tool["inputSchema"].get("oneOf")
        if not isinstance(branches, list):
            continue
        for branch in branches:
            if not isinstance(branch, dict):
                continue
            properties = branch.get("properties")
            if not isinstance(properties, dict):
                continue
            mode = properties.get("mode")
            section_schema = properties.get("sections")
            if not isinstance(mode, dict) or mode.get("const") != "inspect":
                continue
            if not isinstance(section_schema, dict):
                continue
            items = section_schema.get("items")
            values = items.get("enum") if isinstance(items, dict) else None
            if not isinstance(values, list):
                continue
            for section in sorted(sections):
                if section not in values:
                    values.append(section)
            section_schema["maxItems"] = len(values)
    return tools


def compose_companion_capabilities(native_capabilities: dict[str, object]) -> None:
    """Annotate bounded native records with effective Python-catalog readiness."""
    companions = native_capabilities.get("companions")
    if not isinstance(companions, list):
        return
    for companion in companions[:64]:
        if not isinstance(companion, dict):
            continue
        extension_id = companion.get("extension_id")
        known = EXTENSION_CATALOG.get(extension_id) if isinstance(extension_id, str) else None
        python_known = known is not None
        schema_match = (
            python_known
            and companion.get("schema_revision") == known["schema_revision"]
            and companion.get("companion_api_version") == COMPANION_API_VERSION
        )
        native_ready = companion.get("ready") is True
        companion["python_known"] = python_known
        companion["effective_ready"] = native_ready and schema_match
        if native_ready and not python_known:
            companion["effective_unavailable_reason"] = "python_catalog_unknown"
        elif native_ready and not schema_match:
            companion["effective_unavailable_reason"] = "python_schema_mismatch"
        else:
            companion["effective_unavailable_reason"] = companion.get("unavailable_reason", "")
