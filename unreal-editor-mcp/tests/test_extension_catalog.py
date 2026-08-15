import unittest
import json

from unreal_editor_mcp.extension_catalog import (
    COMPANION_API_VERSION,
    EXTENSION_SCHEMA_REVISION,
    compose_extension_tools,
)
from unreal_editor_mcp.schema_validation import SchemaValidationError, validate_tool_arguments
from unreal_editor_mcp.server import MCPServer
from unreal_editor_mcp.errors import DomainError, ErrorCode
from unreal_editor_mcp.project import ProjectIdentity
from unreal_editor_mcp.tool_catalog import tools_for_configuration


def capabilities(*, ready=True, schema=EXTENSION_SCHEMA_REVISION, api=COMPANION_API_VERSION):
    contributions = [
        {"tool_family": "blueprint_inspect", "operation": "inspect_test_asset", "access": "read"},
        {"tool_family": "blueprint_default_edit", "operation": "set_test_asset_value", "access": "mutation"},
        {"tool_family": "blueprint_inspect", "operation": "inspect_test_component", "access": "read"},
        {"tool_family": "blueprint_component_edit", "operation": "set_test_component_value", "access": "mutation"},
        {"tool_family": "blueprint_inspect", "operation": "inspect_test_contribution", "access": "read"},
        {"tool_family": "blueprint_default_edit", "operation": "set_test_contribution_value", "access": "mutation"},
    ]
    return {
        "companion_api_version": api,
        "companions": [{
            "extension_id": "unreal-mcp-test",
            "companion_api_version": api,
            "schema_revision": schema,
            "ready": ready,
            "asset_families": [],
            "contributions": contributions,
        }],
    }


def inspection_family(family_id, native_class):
    return {
        "family_id": family_id,
        "native_class": native_class,
        "class_policy": "exact_and_derived",
        "priority": 200,
        "operations": {"inspect": True, "create": False, "edit": False},
        "creation_persistence": "none",
        "editing_persistence": "none",
        "limits": {"records": 32},
        "selector_routes": [family_id],
        "stable_nested_identity_kinds": [],
    }


def gas_capabilities(*, ready=True, schema=EXTENSION_SCHEMA_REVISION, api=COMPANION_API_VERSION):
    return {
        "companion_api_version": api,
        "companions": [{
            "extension_id": "unreal-mcp-gas",
            "companion_api_version": api,
            "schema_revision": schema,
            "ready": ready,
            "asset_families": [
                inspection_family(
                    "gameplay_ability", "/Script/GameplayAbilities.GameplayAbility",
                ),
                inspection_family(
                    "gameplay_effect", "/Script/GameplayAbilities.GameplayEffect",
                ),
                inspection_family(
                    "attribute_set", "/Script/GameplayAbilities.AttributeSet",
                ),
                inspection_family(
                    "gameplay_cue_notify_actor",
                    "/Script/GameplayAbilities.GameplayCueNotify_Actor",
                ),
                inspection_family(
                    "gameplay_cue_notify_static",
                    "/Script/GameplayAbilities.GameplayCueNotify_Static",
                ),
                inspection_family(
                    "gameplay_effect_execution_calculation",
                    "/Script/GameplayAbilities.GameplayEffectExecutionCalculation",
                ),
                inspection_family(
                    "gameplay_mod_magnitude_calculation",
                    "/Script/GameplayAbilities.GameplayModMagnitudeCalculation",
                ),
            ],
            "contributions": [],
        }],
    }


def commonui_capabilities(
    *, ready=True, schema=EXTENSION_SCHEMA_REVISION, api=COMPANION_API_VERSION,
):
    return {
        "companion_api_version": api,
        "companions": [{
            "extension_id": "unreal-mcp-commonui",
            "companion_api_version": api,
            "schema_revision": schema,
            "ready": ready,
            "asset_families": [inspection_family(
                "commonui_widget", "/Script/UMG.UserWidget",
            )],
            "contributions": [],
        }],
    }


def enhanced_input_capabilities(
    *, ready=True, schema=EXTENSION_SCHEMA_REVISION, api=COMPANION_API_VERSION,
):
    return {
        "companion_api_version": api,
        "companions": [{
            "extension_id": "unreal-mcp-enhanced-input",
            "companion_api_version": api,
            "schema_revision": schema,
            "ready": ready,
            "asset_families": [
                inspection_family("input_action", "/Script/EnhancedInput.InputAction"),
                inspection_family(
                    "input_mapping_context", "/Script/EnhancedInput.InputMappingContext",
                ),
                inspection_family(
                    "input_modifier_blueprint", "/Script/EnhancedInput.InputModifier",
                ),
                inspection_family(
                    "input_trigger_blueprint", "/Script/EnhancedInput.InputTrigger",
                ),
                inspection_family(
                    "player_mappable_input_config",
                    "/Script/EnhancedInput.PlayerMappableInputConfig",
                ),
            ],
            "contributions": [],
        }],
    }


class ExtensionCatalogTests(unittest.TestCase):
    def tool(self, writable, name, native=None):
        base = tools_for_configuration(writable=writable, lifecycle_enabled=False)
        tools = compose_extension_tools(base, capabilities() if native is None else native, writable=writable)
        return next(tool for tool in tools if tool["name"] == name)

    def test_readonly_intersection_adds_only_exact_read_contributions(self):
        tools = compose_extension_tools(
            tools_for_configuration(writable=False, lifecycle_enabled=False),
            capabilities(), writable=False,
        )
        self.assertNotIn("blueprint_inspect", {tool["name"] for tool in tools})
        self.assertNotIn("unreal-mcp-test", json.dumps(tools))
        self.assertNotIn("blueprint_default_edit", {
            tool["name"] for tool in tools
        })

    def test_writable_intersection_adds_exact_mutation_shape(self):
        tool = self.tool(True, "blueprint_default_edit")
        valid = {
            "extension_id": "unreal-mcp-test",
            "extension_schema_revision": EXTENSION_SCHEMA_REVISION,
            "operation": "set_test_asset_value",
            "operation_id": "a" * 32,
            "asset_path": "/Game/Test.Asset",
            "expected_snapshot": "b" * 40,
            "value": 7,
        }
        validate_tool_arguments(valid, tool["inputSchema"])
        for forged in (
            {**valid, "extension_id": "forged"},
            {**valid, "operation": "arbitrary"},
            {**valid, "value": 1000001},
            {**valid, "extra": True},
        ):
            with self.assertRaises(SchemaValidationError):
                validate_tool_arguments(forged, tool["inputSchema"])

    def test_absent_rejected_or_mismatched_native_extension_adds_no_schema(self):
        cases = ({}, capabilities(ready=False), capabilities(schema=1), capabilities(api=1))
        request = {
            "extension_id": "unreal-mcp-test", "extension_schema_revision": EXTENSION_SCHEMA_REVISION,
            "operation": "set_test_asset_value", "operation_id": "a" * 32,
            "asset_path": "/Game/Test.Asset", "expected_snapshot": "b" * 40, "value": 7,
        }
        for native in cases:
            with self.subTest(native=native):
                tool = self.tool(True, "blueprint_default_edit", native)
                with self.assertRaises(SchemaValidationError):
                    validate_tool_arguments(request, tool["inputSchema"])

    def test_gas_companion_inspection_contributions_are_not_published(self):
        for native in (gas_capabilities(), gas_capabilities(ready=False), gas_capabilities(schema=1)):
            tools = compose_extension_tools(
                tools_for_configuration(writable=False, lifecycle_enabled=False), native, writable=False,
            )
            self.assertNotIn("blueprint_inspect", {tool["name"] for tool in tools})
            self.assertNotIn("unreal-mcp-gas", json.dumps(tools))

    def test_gas_companion_never_adds_a_mutation_branch(self):
        tools = compose_extension_tools(
            tools_for_configuration(writable=True, lifecycle_enabled=False),
            gas_capabilities(), writable=True,
        )
        self.assertNotIn("blueprint_inspect", {tool["name"] for tool in tools})
        default_edit = next(tool for tool in tools if tool["name"] == "blueprint_default_edit")
        self.assertNotIn("unreal-mcp-gas", json.dumps(default_edit))

    def test_gas_adapter_keeps_the_shared_asset_inspect_schema_stable(self):
        tool = self.tool(False, "asset_inspect", gas_capabilities())
        self.assertNotIn("gameplay_effect", json.dumps(tool))

    def test_commonui_adapter_keeps_the_shared_asset_inspect_schema_stable(self):
        tool = self.tool(False, "asset_inspect", commonui_capabilities())
        self.assertNotIn("commonui_widget", json.dumps(tool))

    def test_commonui_companion_never_adds_a_mutation_branch(self):
        tools = compose_extension_tools(
            tools_for_configuration(writable=True, lifecycle_enabled=False),
            commonui_capabilities(), writable=True,
        )
        for tool in tools:
            self.assertNotIn("unreal-mcp-commonui", json.dumps(tool))

    def test_enhanced_input_adapter_keeps_the_shared_asset_inspect_schema_stable(self):
        tool = self.tool(False, "asset_inspect", enhanced_input_capabilities())
        self.assertNotIn("input_mapping_context", json.dumps(tool))

    def test_enhanced_input_companion_never_adds_a_mutation_branch(self):
        tools = compose_extension_tools(
            tools_for_configuration(writable=True, lifecycle_enabled=False),
            enhanced_input_capabilities(), writable=True,
        )
        for tool in tools:
            self.assertNotIn("unreal-mcp-enhanced-input", json.dumps(tool))

    def test_server_rejects_forged_extensions_before_dispatch_and_routes_known_exact_schema(self):
        class Bridge:
            def __init__(self):
                self.calls = []

            def call(self, command, arguments=None):
                self.calls.append((command, arguments))
                if command == "capabilities":
                    return capabilities()
                return {"routed": command, "operation": arguments["operation"]}

            def close(self):
                pass

        bridge = Bridge()
        server = MCPServer(bridge, writable=True)
        server.handle({"jsonrpc": "2.0", "id": 1, "method": "tools/list"})
        valid = {
            "extension_id": "unreal-mcp-test", "extension_schema_revision": EXTENSION_SCHEMA_REVISION,
            "operation": "set_test_asset_value", "operation_id": "a" * 32,
            "asset_path": "/Game/Test.Asset", "expected_snapshot": "b" * 40, "value": 7,
        }
        response = server.handle({
            "jsonrpc": "2.0", "id": 2, "method": "tools/call",
            "params": {"name": "blueprint_default_edit", "arguments": valid},
        })
        payload = json.loads(response["result"]["content"][0]["text"])
        self.assertEqual(payload["operation"], "set_test_asset_value")
        dispatched = len([call for call in bridge.calls if call[0] == "blueprint_default_edit"])
        forged = {**valid, "extension_id": "forged"}
        rejected = server.handle({
            "jsonrpc": "2.0", "id": 3, "method": "tools/call",
            "params": {"name": "blueprint_default_edit", "arguments": forged},
        })
        self.assertEqual(rejected["error"]["code"], -32602)
        self.assertEqual(
            len([call for call in bridge.calls if call[0] == "blueprint_default_edit"]), dispatched,
        )

    def test_capability_transition_emits_one_bounded_tool_list_notification(self):
        class Bridge:
            available = True

            def call(self, command, arguments=None):
                if not self.available:
                    raise DomainError("offline", code=ErrorCode.EDITOR_UNAVAILABLE)
                return capabilities()

            def close(self):
                pass

        bridge = Bridge()
        server = MCPServer(bridge, project_identity=ProjectIdentity("Fixture", "a" * 40), writable=True)
        server.handle({
            "jsonrpc": "2.0", "id": 4, "method": "tools/call",
            "params": {"name": "capabilities", "arguments": {}},
        })
        self.assertEqual(server.drain_notifications(), [{
            "jsonrpc": "2.0", "method": "notifications/tools/list_changed",
        }])
        server.handle({
            "jsonrpc": "2.0", "id": 5, "method": "tools/call",
            "params": {"name": "capabilities", "arguments": {}},
        })
        self.assertEqual(server.drain_notifications(), [])
        bridge.available = False
        server.handle({
            "jsonrpc": "2.0", "id": 6, "method": "tools/call",
            "params": {"name": "capabilities", "arguments": {}},
        })
        self.assertEqual(server.drain_notifications(), [{
            "jsonrpc": "2.0", "method": "notifications/tools/list_changed",
        }])


if __name__ == "__main__":
    unittest.main()
