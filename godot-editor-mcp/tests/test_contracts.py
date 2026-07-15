from __future__ import annotations

import re
import unittest
from pathlib import Path

from godot_editor_mcp import __version__
from godot_editor_mcp.bridge import MAX_REQUEST_BYTES, MAX_RESPONSE_BYTES
from godot_editor_mcp.errors import ErrorCode
from godot_editor_mcp.tool_catalog import (
    BRIDGE_COMMANDS,
    EXPECTED_BRIDGE_COMMANDS,
    EXPECTED_BRIDGE_LIMITS,
    EXPECTED_EDITOR_ERROR_CODES,
    MODE_TOOL_NAMES,
    MODES,
    SPEC_BY_NAME,
    TOOL_SPECS,
    TOOLS,
    WAIT_PROPERTIES,
    bridge_contract_mismatches,
)


ROOT = Path(__file__).parents[1]

EXPECTED_CATALOG_ORDER = (
    "capabilities", "editor_state", "get_diagnostics", "reload_project",
    "list_assets", "asset_info", "scan_asset", "import_asset", "create_folder",
    "create_resource", "create_scene", "open_scene", "scene_tree", "add_node",
    "instantiate_scene", "node_info", "set_property", "select_node",
    "scene_control", "project_settings_get", "project_settings_patch",
    "input_map_patch", "start_editor",
)
EXPECTED_MODE_ORDER = {
    "tiny": (
        "capabilities", "editor_state", "get_diagnostics", "reload_project",
        "create_scene", "open_scene", "scene_tree", "add_node",
        "instantiate_scene", "node_info", "set_property", "scene_control",
    ),
    "small": (
        "capabilities", "editor_state", "get_diagnostics", "reload_project",
        "create_scene", "open_scene", "scene_tree", "add_node",
        "instantiate_scene", "node_info", "set_property", "scene_control",
        "list_assets", "asset_info", "scan_asset", "import_asset",
        "create_folder", "create_resource", "project_settings_get",
        "project_settings_patch", "input_map_patch",
    ),
    "large": (
        "capabilities", "editor_state", "get_diagnostics", "reload_project",
        "create_scene", "open_scene", "scene_tree", "add_node",
        "instantiate_scene", "node_info", "set_property", "scene_control",
        "list_assets", "asset_info", "scan_asset", "import_asset",
        "create_folder", "create_resource", "project_settings_get",
        "project_settings_patch", "input_map_patch", "select_node", "start_editor",
    ),
}


class ToolRegistryContractTests(unittest.TestCase):
    def test_registry_names_order_and_modes_are_stable(self) -> None:
        names = tuple(spec.name for spec in TOOL_SPECS)
        self.assertEqual(names, EXPECTED_CATALOG_ORDER)
        self.assertEqual(len(names), len(set(names)))
        self.assertEqual(tuple(tool["name"] for tool in TOOLS), names)
        self.assertEqual(MODE_TOOL_NAMES, EXPECTED_MODE_ORDER)
        for smaller, larger in zip(MODES, MODES[1:]):
            self.assertLess(set(MODE_TOOL_NAMES[smaller]), set(MODE_TOOL_NAMES[larger]))

    def test_every_spec_has_one_complete_execution_route(self) -> None:
        self.assertEqual(set(SPEC_BY_NAME), {spec.name for spec in TOOL_SPECS})
        bridge_routes: list[str] = []
        for spec in TOOL_SPECS:
            if spec.target == "bridge":
                self.assertIsNotNone(spec.bridge_command, spec.name)
                self.assertIsNone(spec.local_handler, spec.name)
                bridge_routes.append(str(spec.bridge_command))
            else:
                self.assertIsNone(spec.bridge_command, spec.name)
                self.assertIsNotNone(spec.local_handler, spec.name)
        self.assertEqual(len(bridge_routes), len(set(bridge_routes)))
        self.assertEqual(set(BRIDGE_COMMANDS), {
            spec.name for spec in TOOL_SPECS if spec.target == "bridge"
        })
        self.assertEqual(set(EXPECTED_BRIDGE_COMMANDS), {
            *bridge_routes, "reload_status",
        })

    def test_path_and_wait_policies_match_schemas(self) -> None:
        for spec in TOOL_SPECS:
            properties = spec.inputSchema.get("properties", {})
            assert isinstance(properties, dict)
            if spec.path_kind in {"folder", "file", "new_file"}:
                self.assertIsNotNone(spec.path_field, spec.name)
                self.assertIn(spec.path_field, properties, spec.name)
            elif spec.path_kind in {"asset_import", "create_folder"}:
                self.assertEqual(spec.target, "assets", spec.name)
            has_wait_fields = all(
                properties.get(name) == schema for name, schema in WAIT_PROPERTIES.items()
            )
            self.assertEqual(has_wait_fields, spec.wait_strategy != "none", spec.name)

    def test_schema_limits_match_bridge_limits(self) -> None:
        self.assertEqual(MAX_REQUEST_BYTES, EXPECTED_BRIDGE_LIMITS["request_bytes"])
        self.assertEqual(MAX_RESPONSE_BYTES, EXPECTED_BRIDGE_LIMITS["response_bytes"])
        schemas = {spec.name: spec.inputSchema for spec in TOOL_SPECS}
        self.assertEqual(
            schemas["list_assets"]["properties"]["limit"]["maximum"],
            EXPECTED_BRIDGE_LIMITS["assets"],
        )
        self.assertEqual(
            schemas["get_diagnostics"]["properties"]["limit"]["maximum"],
            EXPECTED_BRIDGE_LIMITS["diagnostics"],
        )
        self.assertEqual(
            schemas["project_settings_patch"]["properties"]["changes"]["maxItems"],
            EXPECTED_BRIDGE_LIMITS["setting_changes"],
        )
        input_properties = schemas["input_map_patch"]["properties"]
        self.assertEqual(
            input_properties["add_events"]["maxItems"],
            EXPECTED_BRIDGE_LIMITS["input_events"],
        )
        self.assertEqual(
            input_properties["remove_events"]["maxItems"],
            EXPECTED_BRIDGE_LIMITS["input_events"],
        )

    def test_live_capability_comparator_covers_full_contract(self) -> None:
        capabilities = {
            "bridge_version": __version__,
            "bridge_protocol_version": "1",
            "commands": list(EXPECTED_BRIDGE_COMMANDS),
            "limits": dict(EXPECTED_BRIDGE_LIMITS),
            "error_codes": list(EXPECTED_EDITOR_ERROR_CODES),
        }
        self.assertEqual(
            bridge_contract_mismatches(capabilities, expected_version=__version__), []
        )
        capabilities["commands"] = capabilities["commands"][:-1]
        mismatches = bridge_contract_mismatches(
            capabilities, expected_version=__version__
        )
        self.assertEqual(len(mismatches), 1)
        self.assertTrue(mismatches[0].startswith("commands:"))

    def test_editor_error_codes_are_known_to_python(self) -> None:
        python_codes = {
            value for name, value in vars(ErrorCode).items()
            if name.isupper() and isinstance(value, str)
        }
        self.assertLessEqual(set(EXPECTED_EDITOR_ERROR_CODES), python_codes)


class ReleaseConsistencyTests(unittest.TestCase):
    def test_package_plugin_runtime_and_documents_use_one_version(self) -> None:
        pyproject = (ROOT / "pyproject.toml").read_text(encoding="utf-8")
        plugin = (ROOT / "plugin/addons/godot_mcp/plugin.cfg").read_text(
            encoding="utf-8"
        )
        runtime = (ROOT / "plugin/addons/godot_mcp/godot_mcp.gd").read_text(
            encoding="utf-8"
        )
        history = (ROOT / "HISTORY.md").read_text(encoding="utf-8")
        roadmap = (ROOT / "ROADMAP.md").read_text(encoding="utf-8")
        observed = {
            re.search(r'^version = "([^"]+)"$', pyproject, re.MULTILINE).group(1),
            re.search(r'^version="([^"]+)"$', plugin, re.MULTILINE).group(1),
            re.search(r'^const BRIDGE_VERSION := "([^"]+)"$', runtime, re.MULTILINE).group(1),
            re.search(r'^## ([0-9]+\.[0-9]+\.[0-9]+)', history, re.MULTILINE).group(1),
            __version__,
        }
        self.assertEqual(observed, {__version__})
        self.assertIn(f"Completed in {__version__}.", roadmap)


if __name__ == "__main__":
    unittest.main()
