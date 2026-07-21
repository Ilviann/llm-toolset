import io
import json
import unittest

from unreal_editor_mcp.errors import BridgeError, ErrorCode
from unreal_editor_mcp.server import MCPServer
from unreal_editor_mcp.stdio import MAX_MCP_MESSAGE_CHARS, serve


class FakeBridge:
    def __init__(self):
        self.calls = []
        self.closed = False

    def call(self, command, arguments=None):
        self.calls.append((command, arguments))
        if command == "capabilities":
            return {"bridge_version": "0.5.0", "commands": [
                "capabilities", "editor_state", "operation_status", "blueprint_inspect",
                "blueprint_create", "blueprint_compile", "blueprint_save",
                "blueprint_component_edit", "blueprint_default_edit",
                "blueprint_member_edit",
            ]}
        if command == "blueprint_inspect":
            return {"mode": "discover", "snapshot_id": "a" * 40, "records": []}
        if command.startswith("blueprint_"):
            return {"asset_path": "/Game/Actors/BP_Light.BP_Light", "snapshot_id": "a" * 40}
        return {"bridge_ready": True}

    def close(self):
        self.closed = True


class ServerStdioTests(unittest.TestCase):
    def test_initialize_list_and_call(self):
        bridge = FakeBridge()
        server = MCPServer(bridge)
        initialized = server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-06-18"}})
        self.assertEqual(initialized["result"]["serverInfo"]["version"], "0.5.0")
        listed = server.handle({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
        self.assertEqual([tool["name"] for tool in listed["result"]["tools"]], [
            "capabilities", "editor_state", "operation_status", "blueprint_inspect",
            "blueprint_create", "blueprint_compile", "blueprint_save",
            "blueprint_component_edit", "blueprint_default_edit",
            "blueprint_member_edit",
        ])
        called = server.handle({"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": "capabilities", "arguments": {}}})
        payload = json.loads(called["result"]["content"][0]["text"])
        self.assertTrue(payload["version_match"])
        self.assertEqual(payload["mcp_protocol_version"], "2025-06-18")

    def test_rejects_schema_and_unknown_tool(self):
        server = MCPServer(FakeBridge())
        for params in (
            {"name": "capabilities", "arguments": {"unexpected": True}},
            {"name": "blueprint_component_edit", "arguments": {}},
        ):
            response = server.handle({"jsonrpc": "2.0", "id": 1, "method": "tools/call", "params": params})
            self.assertEqual(response["error"]["code"], -32602)

    def test_blueprint_inspect_schema_accepts_exact_modes_and_cursor(self):
        server = MCPServer(FakeBridge())
        valid = (
            {"mode": "discover", "package_path": "/Game/Actors", "asset_name": "BP_Light", "page_size": 10},
            {"mode": "discover", "package_path": "/Engine", "asset_name": "BP_Light"},
            {"mode": "inspect", "asset_path": "/Game/Actors/BP_Light.BP_Light", "sections": ["summary", "nodes"], "include_inherited": True},
            {"mode": "inspect", "asset_path": "/Game/Actors/BP_Light.BP_Light", "sections": ["variables"], "member_id": "e" * 32},
            {"mode": "inspect", "asset_path": "/ProjectPlugin/BP_Light.BP_Light"},
            {"cursor": "a" * 32, "page_size": 25},
        )
        for arguments in valid:
            with self.subTest(arguments=arguments):
                response = server.handle({"jsonrpc": "2.0", "id": 4, "method": "tools/call", "params": {"name": "blueprint_inspect", "arguments": arguments}})
                self.assertNotIn("error", response)
        invalid = (
            {},
            {"mode": "discover", "asset_path": "/Game/A.A"},
            {"mode": "inspect", "asset_path": "Engine/A.A"},
            {"mode": "inspect", "asset_path": "/Game/../Engine/A.A"},
            {"cursor": "short"},
            {"cursor": "a" * 32, "mode": "discover"},
            {"mode": "inspect", "asset_path": "/Game/A.A", "page_size": 101},
        )
        for arguments in invalid:
            with self.subTest(arguments=arguments):
                response = server.handle({"jsonrpc": "2.0", "id": 5, "method": "tools/call", "params": {"name": "blueprint_inspect", "arguments": arguments}})
                self.assertEqual(response["error"]["code"], -32602)

    def test_released_mutation_schemas_are_exact(self):
        server = MCPServer(FakeBridge())
        operation_id = "a" * 32
        snapshot = "b" * 40
        valid = (
            ("blueprint_create", {"operation_id": operation_id, "parent_class": "/Script/Engine.Actor", "package_path": "/Game/Actors/BP_Light"}),
            ("blueprint_create", {"operation_id": operation_id, "parent_class": "/Game/Actors/BP_Parent.BP_Parent_C", "package_path": "/LocalPlugin/BP_Child"}),
            ("blueprint_compile", {"operation_id": operation_id, "asset_path": "/Game/Actors/BP_Light.BP_Light", "expected_snapshot": snapshot}),
            ("blueprint_save", {"operation_id": operation_id, "asset_path": "/Game/Actors/BP_Light", "expected_snapshot": snapshot}),
            ("blueprint_component_edit", {"operation_id": operation_id, "asset_path": "/Game/Actors/BP_Light", "expected_snapshot": snapshot,
                "operation": "add", "component_class": "/Script/Engine.SceneComponent", "name": "Root"}),
            ("blueprint_component_edit", {"operation_id": operation_id, "asset_path": "/Game/Actors/BP_Light", "expected_snapshot": snapshot,
                "operation": "set_property", "component_id": "c" * 32, "property_name": "bVisible", "value": False}),
            ("blueprint_default_edit", {"operation_id": operation_id, "asset_path": "/Game/Actors/BP_Light", "expected_snapshot": snapshot,
                "property_name": "InitialLifeSpan", "value": 12.5}),
            ("blueprint_member_edit", {"operation_id": operation_id, "asset_path": "/Game/Actors/BP_Light", "expected_snapshot": snapshot,
                "operation": "add", "name": "Health", "type": {"category": "int", "container": "none"},
                "default": {"kind": "literal", "value": 100},
                "metadata": {"category": "Stats", "instance_editable": True, "blueprint_visible": True, "replication": "replicated"}}),
            ("blueprint_member_edit", {"operation_id": operation_id, "asset_path": "/Game/Actors/BP_Light", "expected_snapshot": snapshot,
                "operation": "rename", "member_id": "e" * 32, "new_name": "HitPoints"}),
            ("blueprint_member_edit", {"operation_id": operation_id, "asset_path": "/Game/Actors/BP_Light", "expected_snapshot": snapshot,
                "operation": "update", "member_id": "e" * 32, "field": "type",
                "type": {"category": "string", "container": "array"}, "policy": "reject_if_referenced"}),
            ("blueprint_member_edit", {"operation_id": operation_id, "asset_path": "/Game/Actors/BP_Light", "expected_snapshot": snapshot,
                "operation": "update", "member_id": "e" * 32, "field": "default",
                "default": {"kind": "array", "items": [{"kind": "literal", "value": "a"}]}}),
            ("blueprint_member_edit", {"operation_id": operation_id, "asset_path": "/Game/Actors/BP_Light", "expected_snapshot": snapshot,
                "operation": "update", "member_id": "e" * 32, "field": "metadata", "metadata": {"save_game": True}}),
            ("blueprint_member_edit", {"operation_id": operation_id, "asset_path": "/Game/Actors/BP_Light", "expected_snapshot": snapshot,
                "operation": "remove", "member_id": "e" * 32, "policy": "reject_if_referenced"}),
            ("operation_status", {"operation_id": operation_id, "bridge_instance_id": "d" * 32}),
        )
        for name, arguments in valid:
            with self.subTest(name=name, arguments=arguments):
                response = server.handle({"jsonrpc": "2.0", "id": 6, "method": "tools/call", "params": {"name": name, "arguments": arguments}})
                self.assertNotIn("error", response)
        invalid = (
            ("blueprint_create", {}),
            ("blueprint_create", {"operation_id": operation_id, "parent_class": "Actor", "package_path": "/Game/BP_A"}),
            ("blueprint_create", {"operation_id": operation_id, "parent_class": "/Script/Engine.Actor", "package_path": "/Game/BP_A.BP_A"}),
            ("blueprint_compile", {"operation_id": operation_id, "asset_path": "/Game/../Engine/BP_A.BP_A", "expected_snapshot": snapshot}),
            ("blueprint_save", {"operation_id": operation_id, "asset_path": "/Game/BP_A.BP_A", "expected_snapshot": snapshot, "unexpected": True}),
            ("blueprint_component_edit", {"operation_id": operation_id, "asset_path": "/Game/BP_A", "expected_snapshot": snapshot,
                "operation": "remove", "component_id": "short"}),
            ("blueprint_default_edit", {"operation_id": operation_id, "asset_path": "/Game/BP_A", "expected_snapshot": snapshot,
                "property_name": "Unsafe", "value": {"nested": True}}),
            ("blueprint_member_edit", {"operation_id": operation_id, "asset_path": "/Game/BP_A", "expected_snapshot": snapshot,
                "operation": "add", "name": "Bad", "type": {"category": "wildcard", "container": "none"}}),
            ("blueprint_member_edit", {"operation_id": operation_id, "asset_path": "/Game/BP_A", "expected_snapshot": snapshot,
                "operation": "remove", "member_id": "e" * 32, "policy": "cascade"}),
            ("blueprint_member_edit", {"operation_id": operation_id, "asset_path": "/Game/BP_A", "expected_snapshot": snapshot,
                "operation": "update", "member_id": "e" * 32, "field": "metadata", "metadata": {}}),
        )
        for name, arguments in invalid:
            with self.subTest(name=name, arguments=arguments):
                response = server.handle({"jsonrpc": "2.0", "id": 7, "method": "tools/call", "params": {"name": name, "arguments": arguments}})
                self.assertEqual(response["error"]["code"], -32602)

    def test_domain_error_is_tool_error(self):
        class ErrorBridge(FakeBridge):
            def call(self, command, arguments=None):
                raise BridgeError("offline", code=ErrorCode.EDITOR_UNAVAILABLE, retryable=True)
        response = MCPServer(ErrorBridge()).handle({"jsonrpc": "2.0", "id": 1, "method": "tools/call", "params": {"name": "editor_state", "arguments": {}}})
        self.assertTrue(response["result"]["isError"])
        payload = json.loads(response["result"]["content"][0]["text"])
        self.assertEqual(payload["code"], "editor_unavailable")

    def test_stdio_stdout_is_protocol_only_and_closes(self):
        bridge = FakeBridge()
        source = io.StringIO("not json\n" + json.dumps({"jsonrpc": "2.0", "id": 2, "method": "ping"}) + "\n")
        output, diagnostics = io.StringIO(), io.StringIO()
        serve(MCPServer(bridge), input_stream=source, output_stream=output, error_stream=diagnostics)
        messages = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual([message.get("id") for message in messages], [None, 2])
        self.assertEqual(diagnostics.getvalue(), "")
        self.assertTrue(bridge.closed)

    def test_stdio_rejects_and_drains_oversized_line(self):
        source = io.StringIO("x" * (MAX_MCP_MESSAGE_CHARS + 10) + "\n" + json.dumps({"jsonrpc": "2.0", "id": 2, "method": "ping"}) + "\n")
        output = io.StringIO()
        serve(MCPServer(FakeBridge()), input_stream=source, output_stream=output, error_stream=io.StringIO())
        messages = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual(messages[0]["error"]["code"], -32700)
        self.assertEqual(messages[1]["id"], 2)

    def test_notifications_produce_no_output(self):
        output = io.StringIO()
        serve(MCPServer(FakeBridge()), input_stream=io.StringIO('{"jsonrpc":"2.0","method":"notifications/initialized"}\n'), output_stream=output, error_stream=io.StringIO())
        self.assertEqual(output.getvalue(), "")
