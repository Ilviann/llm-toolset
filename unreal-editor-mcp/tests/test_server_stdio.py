import io
import json
import unittest

import unreal_editor_mcp
from unreal_editor_mcp.errors import BridgeError, ErrorCode
from unreal_editor_mcp.project import ProjectIdentity
from unreal_editor_mcp.server import MCPServer
from unreal_editor_mcp.stdio import MAX_MCP_MESSAGE_CHARS, serve
from unreal_editor_mcp.tool_catalog import TOOLS_WITH_LIFECYCLE


class FakeBridge:
    def __init__(self):
        self.calls = []
        self.closed = False

    def call(self, command, arguments=None):
        self.calls.append((command, arguments))
        if command == "capabilities":
            return {"bridge_version": unreal_editor_mcp.__version__, "commands": [
                "capabilities", "editor_state", "operation_status", "operation_cancel",
                "asset_references", "asset_delete",
                "level_inspect", "level_open", "level_manage", "level_actor_edit", "level_save",
                "blueprint_inspect", "blueprint_action_catalog", "blueprint_graph_edit",
                "blueprint_block_replace",
                "blueprint_create", "blueprint_compile", "blueprint_save",
                "blueprint_component_edit", "blueprint_default_edit",
                "blueprint_member_edit", "widget_tree_edit",
                "gameplay_framework_edit", "game_data_inspect", "game_data_edit",
            ]}
        if command == "blueprint_inspect":
            return {"mode": "discover", "snapshot_id": "a" * 40, "records": []}
        if command.startswith("blueprint_") or command == "widget_tree_edit":
            return {"asset_path": "/Game/Actors/BP_Light.BP_Light", "snapshot_id": "a" * 40}
        return {"bridge_ready": True}

    def close(self):
        self.closed = True


class FakeLifecycle:
    def __init__(self):
        self.calls = []

    def availability(self):
        return {"enabled": True, "launch_configured": True}

    def execute(self, arguments):
        self.calls.append(arguments)
        return {"state": "already_stopped"}

    def close(self):
        pass


class ServerStdioTests(unittest.TestCase):
    def test_initialize_list_and_call(self):
        bridge = FakeBridge()
        server = MCPServer(bridge, project_identity=ProjectIdentity("Example Project", "a" * 40))
        initialized = server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-06-18"}})
        self.assertEqual(initialized["result"]["serverInfo"]["version"], unreal_editor_mcp.__version__)
        listed = server.handle({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
        self.assertEqual([tool["name"] for tool in listed["result"]["tools"]], [
            "capabilities", "editor_state", "operation_status", "asset_references",
            "level_inspect", "level_open", "blueprint_inspect", "blueprint_action_catalog",
            "game_data_inspect",
        ])
        called = server.handle({"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": "capabilities", "arguments": {}}})
        payload = json.loads(called["result"]["content"][0]["text"])
        self.assertTrue(payload["version_match"])
        self.assertTrue(payload["native_capabilities_available"])
        self.assertEqual(payload["project_name"], "Example Project")
        self.assertEqual(payload["project_hash"], "a" * 40)
        self.assertEqual(payload["mcp_protocol_version"], "2025-06-18")
        self.assertEqual(payload["access_mode"], "readonly")
        self.assertNotIn("tool_mode", payload)

    def test_exact_catalogs_and_internal_access_classification(self):
        readonly = [
            "capabilities", "editor_state", "operation_status", "asset_references",
            "level_inspect", "level_open", "blueprint_inspect", "blueprint_action_catalog",
            "game_data_inspect",
        ]
        writable = [
            "capabilities", "editor_state", "operation_status", "operation_cancel",
            "asset_references", "asset_delete", "level_inspect", "level_open",
            "level_manage", "level_actor_edit", "level_save", "blueprint_inspect",
            "blueprint_action_catalog", "blueprint_graph_edit", "blueprint_block_replace",
            "blueprint_create", "blueprint_compile", "blueprint_save",
            "blueprint_component_edit", "blueprint_default_edit", "blueprint_member_edit",
            "widget_tree_edit", "gameplay_framework_edit", "game_data_inspect",
            "game_data_edit",
        ]
        cases = (
            (MCPServer(FakeBridge()), readonly),
            (MCPServer(FakeBridge(), lifecycle=FakeLifecycle()), [*readonly, "editor_lifecycle"]),
            (MCPServer(FakeBridge(), writable=True), writable),
            (MCPServer(FakeBridge(), writable=True, lifecycle=FakeLifecycle()),
             [*writable, "editor_lifecycle"]),
        )
        for server, expected in cases:
            with self.subTest(expected=expected):
                self.assertEqual([tool["name"] for tool in server.tools], expected)
                for tool in server.tools:
                    self.assertEqual(set(tool), {"name", "description", "inputSchema"})

    def test_online_capabilities_report_independent_access_and_lifecycle_dimensions(self):
        for writable in (False, True):
            for lifecycle_enabled in (False, True):
                with self.subTest(writable=writable, lifecycle_enabled=lifecycle_enabled):
                    lifecycle = FakeLifecycle() if lifecycle_enabled else None
                    response = MCPServer(
                        FakeBridge(), writable=writable, lifecycle=lifecycle,
                    ).handle({
                        "jsonrpc": "2.0", "id": 89, "method": "tools/call",
                        "params": {"name": "capabilities", "arguments": {}},
                    })
                    payload = json.loads(response["result"]["content"][0]["text"])
                    self.assertEqual(
                        payload["access_mode"],
                        "writable" if writable else "readonly",
                    )
                    self.assertEqual(payload["editor_lifecycle"]["enabled"], lifecycle_enabled)
                    self.assertNotIn("tool_mode", payload)

    def test_every_omitted_tool_is_unknown_without_bridge_dispatch(self):
        universe = {tool["name"] for tool in TOOLS_WITH_LIFECYCLE}
        cases = (
            MCPServer(FakeBridge()),
            MCPServer(FakeBridge(), lifecycle=FakeLifecycle()),
            MCPServer(FakeBridge(), writable=True),
            MCPServer(FakeBridge(), writable=True, lifecycle=FakeLifecycle()),
        )
        for server in cases:
            advertised = {tool["name"] for tool in server.tools}
            for name in sorted(universe - advertised):
                with self.subTest(advertised=advertised, omitted=name):
                    before = list(server.bridge.calls)
                    response = server.handle({
                        "jsonrpc": "2.0", "id": 90, "method": "tools/call",
                        "params": {"name": name, "arguments": {"untrusted": True}},
                    })
                    self.assertEqual(response["error"], {"code": -32602, "message": "Unknown tool"})
                    self.assertEqual(server.bridge.calls, before)

    def test_operation_lookup_and_cancellation_are_separate(self):
        identity = {"operation_id": "a" * 32, "bridge_instance_id": "b" * 32}
        readonly = MCPServer(FakeBridge())
        status = readonly.handle({
            "jsonrpc": "2.0", "id": 91, "method": "tools/call",
            "params": {"name": "operation_status", "arguments": identity},
        })
        self.assertNotIn("error", status)
        rejected = readonly.handle({
            "jsonrpc": "2.0", "id": 92, "method": "tools/call",
            "params": {"name": "operation_status", "arguments": {**identity, "cancel": True}},
        })
        self.assertEqual(rejected["error"]["code"], -32602)
        self.assertEqual(readonly.bridge.calls, [("operation_status", identity)])

        writable = MCPServer(FakeBridge(), writable=True)
        cancelled = writable.handle({
            "jsonrpc": "2.0", "id": 93, "method": "tools/call",
            "params": {"name": "operation_cancel", "arguments": identity},
        })
        self.assertNotIn("error", cancelled)
        self.assertEqual(writable.bridge.calls, [("operation_cancel", identity)])

    def test_level_inspect_and_open_schemas_are_exact_and_bounded(self):
        server = MCPServer(FakeBridge(), writable=True)
        map_id = "c" * 40
        snapshot = "d" * 40
        actor_id = map_id + ":" + "e" * 32
        valid = (
            ("level_inspect", {"mode": "discover"}),
            ("level_inspect", {"mode": "discover", "package_path": "/Game/Maps",
                               "asset_name": "Example", "page_size": 100}),
            ("level_inspect", {"mode": "current"}),
            ("level_inspect", {"mode": "actors", "map_id": map_id,
                               "expected_snapshot": snapshot, "page_size": 1}),
            ("level_inspect", {"mode": "actors", "map_id": map_id,
                               "expected_snapshot": snapshot, "filters": {
                                   "actor_id": actor_id, "label": "Door",
                                   "class_path": "/Script/Engine.Actor", "tag": "Interactive",
                                   "folder": "Gameplay/Doors", "data_layer": "Gameplay",
                                   "loaded": False,
                                   "region": {
                                       "min": {"x": -100, "y": -100, "z": -100},
                                       "max": {"x": 100, "y": 100, "z": 100},
                                   },
                               }}),
            ("level_inspect", {"mode": "actor", "map_id": map_id,
                               "expected_snapshot": snapshot, "actor_id": actor_id,
                               "property_names": ["Tags"]}),
            ("level_inspect", {"mode": "component", "map_id": map_id,
                               "expected_snapshot": snapshot, "actor_id": actor_id,
                               "component_id": "f" * 32, "property_names": ["RelativeLocation"]}),
            ("level_inspect", {"cursor": "a" * 32, "page_size": 1}),
            ("level_open", {"operation_id": "b" * 32,
                            "map_path": "/Game/Maps/Example.Example"}),
        )
        for name, arguments in valid:
            with self.subTest(name=name, arguments=arguments):
                response = server.handle({
                    "jsonrpc": "2.0", "id": 20, "method": "tools/call",
                    "params": {"name": name, "arguments": arguments},
                })
                self.assertNotIn("error", response)
        invalid = (
            ("level_inspect", {}),
            ("level_inspect", {"mode": "current", "page_size": 1}),
            ("level_inspect", {"mode": "actors", "map_id": map_id}),
            ("level_inspect", {"mode": "actors", "map_id": map_id,
                               "expected_snapshot": snapshot,
                               "filters": {"region": {
                                   "min": {"x": 1, "y": 0},
                                   "max": {"x": 0, "y": 1, "z": 1},
                               }}}),
            ("level_inspect", {"mode": "actor", "map_id": map_id,
                               "expected_snapshot": snapshot, "actor_id": "e" * 32}),
            ("level_inspect", {"mode": "component", "map_id": map_id,
                               "expected_snapshot": snapshot, "actor_id": actor_id,
                               "component_id": "short"}),
            ("level_inspect", {"mode": "discover", "package_path": "/Game/../Engine"}),
            ("level_inspect", {"cursor": "short"}),
            ("level_open", {"operation_id": "b" * 32, "map_path": "Game/Maps/Example.Example"}),
            ("level_open", {"operation_id": "short", "map_path": "/Game/Maps/Example.Example"}),
            ("level_open", {"operation_id": "b" * 32, "map_path": "/Game/Maps/Example.Example",
                            "save": True}),
        )
        for name, arguments in invalid:
            with self.subTest(name=name, arguments=arguments):
                response = server.handle({
                    "jsonrpc": "2.0", "id": 21, "method": "tools/call",
                    "params": {"name": name, "arguments": arguments},
                })
                self.assertEqual(response["error"]["code"], -32602)

    def test_level_manage_schema_is_exact_bounded_and_explicit(self):
        server = MCPServer(FakeBridge(), writable=True)
        operation_id = "a" * 32
        snapshot = "b" * 40
        blank = {
            "operation_id": operation_id,
            "operation": "create",
            "destination_path": "/Game/Maps/NewMap.NewMap",
            "source": {"kind": "blank"},
            "creation_options": {
                "world_partition": False,
                "world_partition_streaming": False,
                "external_actors": False,
            },
            "settings": [{"property_name": "DefaultGameMode", "value": "/Script/Engine.GameModeBase"}],
            "open_after_create": False,
            "expected_current_snapshot": snapshot,
        }
        template = {
            "operation_id": operation_id,
            "operation": "create",
            "destination_path": "/Game/Maps/FromTemplate.FromTemplate",
            "source": {"kind": "template", "map_path": "/Game/Maps/Template.Template"},
            "open_after_create": True,
            "expected_current_snapshot": snapshot,
        }
        configure = {
            "operation_id": operation_id,
            "operation": "configure",
            "map_path": "/Game/Maps/NewMap.NewMap",
            "expected_current_snapshot": snapshot,
            "settings": [
                {"property_name": "WorldToMeters", "value": 100},
                {"property_name": "DefaultColorScale", "value": "(X=1,Y=1,Z=1)"},
            ],
            "reload_after_save": True,
        }
        for arguments in (blank, template, configure):
            response = server.handle({
                "jsonrpc": "2.0", "id": 24, "method": "tools/call",
                "params": {"name": "level_manage", "arguments": arguments},
            })
            self.assertNotIn("error", response)
        for arguments in (
            {},
            {**blank, "destination_path": "C:\\Project\\Content\\NewMap.umap"},
            {**blank, "force": True},
            {**blank, "settings": [{"property_name": "WorldPartition", "value": True}]},
            {**configure, "reload_after_save": "yes"},
            {**configure, "settings": configure["settings"] * 9},
            {**template, "creation_options": blank["creation_options"]},
        ):
            response = server.handle({
                "jsonrpc": "2.0", "id": 25, "method": "tools/call",
                "params": {"name": "level_manage", "arguments": arguments},
            })
            self.assertEqual(response["error"]["code"], -32602)

    def test_asset_references_schema_is_exact_and_bounded(self):
        server = MCPServer(FakeBridge(), writable=True)
        valid = (
            {"asset_path": "/Game/Data/DA_Config.DA_Config"},
            {"asset_path": "/Engine/EngineResources/DefaultTexture.DefaultTexture", "page_size": 100},
            {"cursor": "a" * 32},
            {"cursor": "b" * 32, "page_size": 1},
        )
        for arguments in valid:
            with self.subTest(arguments=arguments):
                response = server.handle({"jsonrpc": "2.0", "id": 19, "method": "tools/call",
                    "params": {"name": "asset_references", "arguments": arguments}})
                self.assertNotIn("error", response)
        invalid = (
            {},
            {"asset_path": "C:\\Project\\Content\\A.uasset"},
            {"asset_path": "/Game/Data/../A.A"},
            {"asset_path": "/Game/Data/A.A", "cursor": "a" * 32},
            {"asset_path": "/Game/Data/A.A", "page_size": 101},
            {"cursor": "short"},
            {"cursor": "a" * 32, "asset_path": "/Game/Data/A.A"},
        )
        for arguments in invalid:
            with self.subTest(arguments=arguments):
                response = server.handle({"jsonrpc": "2.0", "id": 20, "method": "tools/call",
                    "params": {"name": "asset_references", "arguments": arguments}})
                self.assertEqual(response["error"]["code"], -32602)

    def test_level_edit_and_save_schemas_are_exact_bounded_and_stale_safe(self):
        server = MCPServer(FakeBridge(), writable=True)
        operation_id = "a" * 32
        map_id = "b" * 40
        snapshot = "c" * 40
        actor_id = map_id + ":" + "d" * 32
        transform = {
            "location": {"x": 1, "y": 2, "z": 3},
            "rotation": {"pitch": 0, "yaw": 90, "roll": 0},
            "scale": {"x": 1, "y": 1, "z": 1},
        }
        edit = {
            "operation_id": operation_id,
            "map_id": map_id,
            "expected_snapshot": snapshot,
            "operations": [
                {"operation": "spawn", "class_path": "/Script/Engine.TextRenderActor",
                 "transform": transform, "label": "Spawned", "tags": ["Authored"],
                 "folder": "MCP/Actors", "data_layers": [],
                 "actor_properties": [{"property_name": "InitialLifeSpan", "value": 5}]},
                {"operation": "transform", "actor_id": actor_id, "transform": transform},
                {"operation": "attach", "actor_id": actor_id,
                 "parent_actor_id": map_id + ":" + "e" * 32},
                {"operation": "component_property", "actor_id": actor_id,
                 "component_id": "f" * 32, "property_name": "WorldSize", "value": 128},
            ],
        }
        save = {
            "operation_id": operation_id,
            "map_id": map_id,
            "expected_snapshot": snapshot,
            "affected_packages": ["/Game/Maps/Test", "/Game/__ExternalActors__/Maps/Test/AA/Actor"],
            "verification": {"mode": "reload", "actors": [{
                "actor_id": actor_id, "label": "Edited", "transform": transform,
                "tags": ["Authored"], "folder": "MCP/Actors",
                "actor_properties": [{"property_name": "InitialLifeSpan", "value": 5}],
                "components": [{"component_id": "f" * 32, "properties": [
                    {"property_name": "WorldSize", "value": 128}]}],
            }]},
        }
        for name, arguments in (("level_actor_edit", edit), ("level_save", save)):
            response = server.handle({"jsonrpc": "2.0", "id": 26, "method": "tools/call",
                "params": {"name": name, "arguments": arguments}})
            self.assertNotIn("error", response)
        invalid = (
            ("level_actor_edit", {**edit, "operations": []}),
            ("level_actor_edit", {**edit, "operations": edit["operations"] * 9}),
            ("level_actor_edit", {**edit, "operations": [
                {"operation": "delete", "actor_id": actor_id, "force": True}]}),
            ("level_actor_edit", {**edit, "map_id": "short"}),
            ("level_actor_edit", {**edit, "operations": [
                {"operation": "transform", "actor_id": actor_id,
                 "transform": {**transform, "scale": {"x": float("inf"), "y": 1, "z": 1}}}]}),
            ("level_save", {**save, "affected_packages": []}),
            ("level_save", {**save, "verification": {"mode": "none", "actors": []}}),
            ("level_save", {**save, "force": True}),
        )
        for name, arguments in invalid:
            response = server.handle({"jsonrpc": "2.0", "id": 27, "method": "tools/call",
                "params": {"name": name, "arguments": arguments}})
            self.assertEqual(response["error"]["code"], -32602)

    def test_asset_delete_schema_is_exact_and_stale_safe(self):
        server = MCPServer(FakeBridge(), writable=True)
        valid = {
            "operation_id": "a" * 32,
            "asset_path": "/Game/Data/DA_Disposable.DA_Disposable",
            "expected_snapshot": "b" * 40,
        }
        response = server.handle({
            "jsonrpc": "2.0", "id": 22, "method": "tools/call",
            "params": {"name": "asset_delete", "arguments": valid},
        })
        self.assertNotIn("error", response)
        for arguments in (
            {},
            {**valid, "operation_id": "short"},
            {**valid, "asset_path": "C:\\Project\\Content\\DA_Disposable.uasset"},
            {**valid, "expected_snapshot": "short"},
            {**valid, "force": True},
        ):
            with self.subTest(arguments=arguments):
                response = server.handle({
                    "jsonrpc": "2.0", "id": 23, "method": "tools/call",
                    "params": {"name": "asset_delete", "arguments": arguments},
                })
                self.assertEqual(response["error"]["code"], -32602)

    def test_rejects_schema_and_unknown_tool(self):
        server = MCPServer(FakeBridge(), writable=True)
        for params in (
            {"name": "capabilities", "arguments": {"unexpected": True}},
            {"name": "blueprint_component_edit", "arguments": {}},
        ):
            response = server.handle({"jsonrpc": "2.0", "id": 1, "method": "tools/call", "params": params})
            self.assertEqual(response["error"]["code"], -32602)

    def test_blueprint_inspect_schema_accepts_exact_modes_and_cursor(self):
        server = MCPServer(FakeBridge(), writable=True)
        valid = (
            {"mode": "discover", "package_path": "/Game/Actors", "asset_name": "BP_Light", "page_size": 10},
            {"mode": "discover", "package_path": "/Engine", "asset_name": "BP_Light"},
            {"mode": "inspect", "asset_path": "/Game/Actors/BP_Light.BP_Light", "sections": ["summary", "nodes"], "include_inherited": True},
            {"mode": "inspect", "asset_path": "/Game/Actors/BP_Light.BP_Light", "sections": ["variables"], "member_id": "e" * 32},
            {"mode": "inspect", "asset_path": "/Game/Actors/BP_Light.BP_Light", "sections": ["functions", "parameters"], "function_id": "f" * 32},
            {"mode": "inspect", "asset_path": "/Game/Actors/BP_Light.BP_Light", "sections": ["local_variables"], "local_id": "d" * 32},
            {"mode": "inspect", "asset_path": "/Game/Actors/BP_Light.BP_Light", "sections": ["macros", "parameters"], "macro_id": "c" * 32},
            {"mode": "inspect", "asset_path": "/Game/Actors/BP_Light.BP_Light", "sections": ["custom_events", "parameters"], "custom_event_id": "9" * 32},
            {"mode": "inspect", "asset_path": "/ProjectPlugin/BP_Light.BP_Light"},
            {"mode": "inspect", "asset_path": "/Game/UI/WBP_HUD.WBP_HUD",
             "sections": ["widget_tree", "widget_defaults"], "widget_id": "7" * 32,
             "property_names": ["RenderOpacity"]},
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
        server = MCPServer(FakeBridge(), writable=True)
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
            ("blueprint_component_edit", {"operation_id": operation_id, "asset_path": "/Game/Actors/BP_Light", "expected_snapshot": snapshot,
                "operation": "set_replication", "component_id": "c" * 32, "replicates": True}),
            ("blueprint_default_edit", {"operation_id": operation_id, "asset_path": "/Game/Actors/BP_Light", "expected_snapshot": snapshot,
                "property_name": "InitialLifeSpan", "value": 12.5}),
            ("blueprint_default_edit", {"operation_id": operation_id, "asset_path": "/Game/Actors/BP_Light", "expected_snapshot": snapshot,
                "replication_setting": "dormancy", "value": "DORM_Awake"}),
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
            ("blueprint_member_edit", {"operation_id": operation_id, "asset_path": "/Game/Actors/BP_Light", "expected_snapshot": snapshot,
                "target": "function", "operation": "add", "name": "Compute",
                "signature": {"access": "public", "pure": False, "const": True, "parameters": [
                    {"name": "Label", "direction": "input", "type": {"category": "string", "container": "none", "reference": True, "const": True}},
                    {"name": "Result", "direction": "output", "type": {"category": "boolean", "container": "none"}},
                ]}, "metadata": {"category": "Logic", "tooltip": "Compute a result"}}),
            ("blueprint_member_edit", {"operation_id": operation_id, "asset_path": "/Game/Actors/BP_Light", "expected_snapshot": snapshot,
                "target": "function", "operation": "update", "function_id": "f" * 32, "field": "signature",
                "signature": {"access": "private", "pure": True, "const": False, "parameters": []},
                "policy": "reject_if_referenced"}),
            ("blueprint_member_edit", {"operation_id": operation_id, "asset_path": "/Game/Actors/BP_Light", "expected_snapshot": snapshot,
                "target": "function", "operation": "remove", "function_id": "f" * 32, "policy": "reject_if_referenced"}),
            ("blueprint_member_edit", {"operation_id": operation_id, "asset_path": "/Game/Actors/BP_Light", "expected_snapshot": snapshot,
                "target": "local_variable", "operation": "add", "function_id": "f" * 32, "name": "Total",
                "type": {"category": "int", "container": "none"}, "default": {"kind": "literal", "value": 1}}),
            ("blueprint_member_edit", {"operation_id": operation_id, "asset_path": "/Game/Actors/BP_Light", "expected_snapshot": snapshot,
                "target": "local_variable", "operation": "remove", "function_id": "f" * 32, "local_id": "d" * 32,
                "policy": "reject_if_referenced"}),
            ("blueprint_member_edit", {"operation_id": operation_id, "asset_path": "/Game/Actors/BP_Light", "expected_snapshot": snapshot,
                "target": "macro", "operation": "add", "name": "ComputeFlow",
                "signature": {"pure": False, "parameters": [
                    {"name": "Count", "direction": "input", "type": {"category": "int", "container": "none"},
                     "default": {"kind": "literal", "value": 1}},
                    {"name": "Result", "direction": "output", "type": {"category": "boolean", "container": "none"}},
                ]}, "metadata": {"category": "Logic", "tooltip": "Compute a flow"}}),
            ("blueprint_member_edit", {"operation_id": operation_id, "asset_path": "/Game/Actors/BP_Light", "expected_snapshot": snapshot,
                "target": "macro", "operation": "update", "macro_id": "c" * 32, "field": "signature",
                "signature": {"pure": True, "parameters": []}, "policy": "reject_if_referenced"}),
            ("blueprint_member_edit", {"operation_id": operation_id, "asset_path": "/Game/Actors/BP_Light", "expected_snapshot": snapshot,
                "target": "custom_event", "operation": "add", "graph_id": "8" * 32, "name": "OnReady",
                "signature": {"parameters": [
                    {"name": "Value", "type": {"category": "string", "container": "none"},
                     "default": {"kind": "literal", "value": "ready"}},
                ]}, "metadata": {"category": "Events", "call_in_editor": True}}),
            ("blueprint_member_edit", {"operation_id": operation_id, "asset_path": "/Game/Actors/BP_Light", "expected_snapshot": snapshot,
                "target": "custom_event", "operation": "add", "graph_id": "8" * 32, "name": "ServerFire",
                "signature": {"parameters": []}, "metadata": {"rpc_mode": "server", "reliability": "reliable"}}),
            ("blueprint_member_edit", {"operation_id": operation_id, "asset_path": "/Game/Actors/BP_Light", "expected_snapshot": snapshot,
                "target": "custom_event", "operation": "remove", "custom_event_id": "9" * 32,
                "policy": "reject_if_referenced"}),
            ("blueprint_member_edit", {"operation_id": operation_id, "asset_path": "/Game/Actors/BP_Light", "expected_snapshot": snapshot,
                "operation": "update", "member_id": "e" * 32, "field": "metadata",
                "metadata": {"replication": "rep_notify", "rep_notify_function": "OnRep_Health", "replication_condition": "COND_OwnerOnly"}}),
            ("operation_status", {"operation_id": operation_id, "bridge_instance_id": "d" * 32}),
            ("gameplay_framework_edit", {"operation_id": operation_id, "project_hash": "e" * 40,
                "setting": "default_game_mode", "class_path": "/Game/Framework/BP_Mode.BP_Mode_C",
                "expected_class": "/Script/Engine.GameModeBase"}),
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
            ("blueprint_default_edit", {"operation_id": operation_id, "asset_path": "/Game/BP_A", "expected_snapshot": snapshot,
                "replication_setting": "net_priority", "value": -1}),
            ("blueprint_member_edit", {"operation_id": operation_id, "asset_path": "/Game/BP_A", "expected_snapshot": snapshot,
                "operation": "add", "name": "Bad", "type": {"category": "wildcard", "container": "none"}}),
            ("blueprint_member_edit", {"operation_id": operation_id, "asset_path": "/Game/BP_A", "expected_snapshot": snapshot,
                "operation": "remove", "member_id": "e" * 32, "policy": "cascade"}),
            ("blueprint_member_edit", {"operation_id": operation_id, "asset_path": "/Game/BP_A", "expected_snapshot": snapshot,
                "operation": "update", "member_id": "e" * 32, "field": "metadata", "metadata": {}}),
            ("blueprint_member_edit", {"operation_id": operation_id, "asset_path": "/Game/BP_A", "expected_snapshot": snapshot,
                "target": "function", "operation": "add", "name": "Bad",
                "signature": {"access": "package", "pure": False, "const": False, "parameters": []}}),
            ("blueprint_member_edit", {"operation_id": operation_id, "asset_path": "/Game/BP_A", "expected_snapshot": snapshot,
                "target": "local_variable", "operation": "remove", "function_id": "f" * 32, "local_id": "d" * 32,
                "policy": "cascade"}),
            ("blueprint_member_edit", {"operation_id": operation_id, "asset_path": "/Game/BP_A", "expected_snapshot": snapshot,
                "target": "macro", "operation": "add", "name": "Bad",
                "signature": {"pure": False, "parameters": []}, "metadata": {"call_in_editor": True}}),
            ("blueprint_member_edit", {"operation_id": operation_id, "asset_path": "/Game/BP_A", "expected_snapshot": snapshot,
                "target": "custom_event", "operation": "add", "graph_id": "8" * 32, "name": "Bad",
                "signature": {"parameters": [{"name": "Bad", "direction": "input", "type": {"category": "int", "container": "none"}}]}}),
            ("blueprint_member_edit", {"operation_id": operation_id, "asset_path": "/Game/BP_A", "expected_snapshot": snapshot,
                "target": "custom_event", "operation": "add", "graph_id": "8" * 32, "name": "BadRpc",
                "signature": {"parameters": []}, "metadata": {"rpc_mode": "broadcast"}}),
            ("gameplay_framework_edit", {"operation_id": operation_id, "project_hash": "short",
                "setting": "default_game_mode", "class_path": "/Script/Engine.GameModeBase", "expected_class": ""}),
        )
        for name, arguments in invalid:
            with self.subTest(name=name, arguments=arguments):
                response = server.handle({"jsonrpc": "2.0", "id": 7, "method": "tools/call", "params": {"name": name, "arguments": arguments}})
                self.assertEqual(response["error"]["code"], -32602)

    def test_widget_tree_schema_is_exact_and_stale_safe(self):
        server = MCPServer(FakeBridge(), writable=True)
        base = {
            "operation_id": "a" * 32,
            "asset_path": "/Game/UI/WBP_HUD.WBP_HUD",
            "expected_snapshot": "b" * 40,
        }
        target = {"kind": "panel", "parent_id": "c" * 32, "index": 0}
        valid = (
            {**base, "operation": "set_root", "widget_class": "/Script/UMG.CanvasPanel",
             "name": "Root"},
            {**base, "operation": "add", "widget_class": "/Script/UMG.TextBlock",
             "name": "Title", "target": target},
            {**base, "operation": "add", "widget_class": "/Game/UI/WBP_Row.WBP_Row_C",
             "name": "Row", "target": {"kind": "named_slot", "slot_id": "d" * 32}},
            {**base, "operation": "remove", "widget_id": "e" * 32,
             "policy": "reject_if_referenced"},
            {**base, "operation": "rename", "widget_id": "e" * 32,
             "new_name": "Heading"},
            {**base, "operation": "reparent", "widget_id": "e" * 32,
             "target": target},
            {**base, "operation": "set_variable", "widget_id": "e" * 32,
             "is_variable": True},
            {**base, "operation": "set_property", "widget_id": "e" * 32,
             "property_name": "RenderOpacity", "value": 0.5},
            {**base, "operation": "set_slot", "slot_id": "f" * 32,
             "property_name": "LayoutData", "value": {
                 "kind": "struct", "fields": {
                     "Offsets": {"kind": "struct", "fields": {
                         "Left": 0, "Top": 0, "Right": 320, "Bottom": 64,
                     }},
                 },
             }},
            {**base, "operation": "set_style", "widget_id": "e" * 32,
             "property_name": "ColorAndOpacity", "value": {
                 "kind": "struct", "fields": {
                     "SpecifiedColor": {"kind": "struct", "fields": {
                         "R": 1.0, "G": 0.5, "B": 0.0, "A": 1.0,
                     }},
                     "ColorUseRule": "UseColor_Specified",
                 },
             }},
            {**base, "operation": "set_style", "widget_id": "e" * 32,
             "property_name": "DefaultOptions", "value": ["Low", "Medium", "High"]},
            {**base, "operation": "bind_property", "widget_id": "e" * 32,
             "target_property": "Text", "source_kind": "property",
             "source_name": "Title"},
            {**base, "operation": "unbind_property", "widget_id": "e" * 32,
             "target_property": "Text"},
            {**base, "operation": "bind_event", "widget_id": "e" * 32,
             "delegate_name": "OnClicked"},
            {**base, "operation": "unbind_event", "widget_id": "e" * 32,
             "delegate_name": "OnClicked", "policy": "reject_if_connected"},
        )
        for arguments in valid:
            with self.subTest(arguments=arguments):
                response = server.handle({
                    "jsonrpc": "2.0", "id": 30, "method": "tools/call",
                    "params": {"name": "widget_tree_edit", "arguments": arguments},
                })
                self.assertNotIn("error", response)
        invalid = (
            {},
            {**base, "operation": "set_root", "widget_class": "CanvasPanel", "name": "Root"},
            {**base, "operation": "add", "widget_class": "/Script/UMG.TextBlock",
             "name": "Title", "target": {"kind": "root"}},
            {**base, "operation": "remove", "widget_id": "short",
             "policy": "reject_if_referenced"},
            {**base, "operation": "remove", "widget_id": "e" * 32,
             "policy": "cascade"},
            {**base, "operation": "set_property", "widget_id": "e" * 32,
             "property_name": "Unsafe", "value": {"nested": True}},
            {**base, "operation": "set_style", "widget_id": "e" * 32,
             "property_name": "Brush", "value": {"nested": True}},
            {**base, "operation": "unbind_event", "widget_id": "e" * 32,
             "delegate_name": "OnClicked", "policy": "cascade"},
            {**base, "operation": "rename", "widget_id": "e" * 32,
             "new_name": "Heading", "extra": True},
        )
        for arguments in invalid:
            with self.subTest(arguments=arguments):
                response = server.handle({
                    "jsonrpc": "2.0", "id": 31, "method": "tools/call",
                    "params": {"name": "widget_tree_edit", "arguments": arguments},
                })
                self.assertEqual(response["error"]["code"], -32602)

    def test_action_catalog_schema_is_exact_and_bounded(self):
        server = MCPServer(FakeBridge(), writable=True)
        base = {
            "asset_path": "/Game/Actors/BP_Light.BP_Light",
            "graph_id": "a" * 32,
            "expected_snapshot": "b" * 40,
        }
        valid = (
            base,
            {**base, "text": "Get Health", "owner_class": "/Game/Actors/BP_Light.BP_Light_C",
             "function": "Compute", "node_family": "function_call", "limit": 1},
            {**base, "member": "Health", "node_family": "variable_get",
             "pin_context": {"node_id": "c" * 32, "pin_id": "d" * 32}, "limit": 50},
            {**base, "function": "ReceiveBeginPlay", "node_family": "event"},
            {**base, "node_family": "flow_control"},
            {**base, "owner_class": "/Script/Engine.Actor", "node_family": "cast"},
            {**base, "function": "MakeLiteralInt", "node_family": "literal"},
            {**base, "function": "Add_DoubleDouble", "node_family": "operator"},
        )
        for arguments in valid:
            with self.subTest(arguments=arguments):
                response = server.handle({"jsonrpc": "2.0", "id": 8, "method": "tools/call",
                    "params": {"name": "blueprint_action_catalog", "arguments": arguments}})
                self.assertNotIn("error", response)
        invalid = (
            {},
            {**base, "graph_id": "short"},
            {**base, "expected_snapshot": "A" * 40},
            {**base, "node_family": "arbitrary_node"},
            {**base, "limit": 51},
            {**base, "pin_context": {"node_id": "c" * 32}},
            {**base, "node_class": "/Script/BlueprintGraph.K2Node_CallFunction"},
        )
        for arguments in invalid:
            with self.subTest(arguments=arguments):
                response = server.handle({"jsonrpc": "2.0", "id": 9, "method": "tools/call",
                    "params": {"name": "blueprint_action_catalog", "arguments": arguments}})
                self.assertEqual(response["error"]["code"], -32602)

    def test_graph_edit_schema_is_exact_and_bounded(self):
        server = MCPServer(FakeBridge(), writable=True)
        base = {
            "operation_id": "a" * 32,
            "asset_path": "/Game/Actors/BP_Light.BP_Light",
            "expected_snapshot": "b" * 40,
            "graph_id": "c" * 32,
        }
        valid = (
            {**base, "operation": "add_node", "action_id": "d" * 32,
             "position": {"x": -1000000, "y": 1000000}},
            {**base, "operation": "move_node", "node_id": "e" * 32,
             "position": {"x": 160, "y": -320}},
            {**base, "operation": "remove_node", "node_id": "e" * 32},
            {**base, "operation": "set_pin_default", "node_id": "e" * 32,
             "pin_id": "f" * 32, "default": {"kind": "literal", "value": 42}},
            {**base, "operation": "set_pin_default", "node_id": "e" * 32,
             "pin_id": "f" * 32, "default": {"kind": "reference", "path": "/Game/Data/DA_Config.DA_Config"}},
            {**base, "operation": "connect_pins", "from_node_id": "d" * 32,
             "from_pin_id": "e" * 32, "to_node_id": "f" * 32, "to_pin_id": "1" * 32},
            {**base, "operation": "connect_pins", "from_node_id": "d" * 32,
             "from_pin_id": "e" * 32, "to_node_id": "f" * 32, "to_pin_id": "1" * 32,
             "automatic_conversion": True},
            {**base, "operation": "disconnect_pins", "from_node_id": "d" * 32,
             "from_pin_id": "e" * 32, "to_node_id": "f" * 32, "to_pin_id": "1" * 32},
        )
        for arguments in valid:
            with self.subTest(arguments=arguments):
                response = server.handle({"jsonrpc": "2.0", "id": 10, "method": "tools/call",
                    "params": {"name": "blueprint_graph_edit", "arguments": arguments}})
                self.assertNotIn("error", response)
        invalid = (
            {},
            {**base, "operation": "add_node", "action_id": "short", "position": {"x": 0, "y": 0}},
            {**base, "operation": "move_node", "node_id": "e" * 32, "position": {"x": 1000001, "y": 0}},
            {**base, "operation": "move_node", "node_id": "e" * 32, "position": {"x": 1.5, "y": 0}},
            {**base, "operation": "remove_node", "node_id": "e" * 32, "position": {"x": 0, "y": 0}},
            {**base, "operation": "set_pin_default", "node_id": "e" * 32,
             "pin_id": "short", "default": {"kind": "literal", "value": 1}},
            {**base, "operation": "set_pin_default", "node_id": "e" * 32,
             "pin_id": "f" * 32, "default": {"kind": "raw", "value": "unsafe"}},
            {**base, "operation": "connect_pins", "from_node_id": "d" * 32,
             "from_pin_id": "e" * 32, "to_node_id": "f" * 32},
            {**base, "operation": "disconnect_pins", "from_node_id": "d" * 32,
             "from_pin_id": "e" * 32, "to_node_id": "f" * 32, "to_pin_id": "1" * 32,
             "automatic_conversion": True},
            {**base, "operation": "connect_pins", "from_node_id": "d" * 32,
             "from_pin_id": "e" * 32, "to_node_id": "f" * 32, "to_pin_id": "1" * 32,
             "automatic_conversion": 1},
            {**base, "operation": "rename_node", "node_id": "e" * 32},
        )
        for arguments in invalid:
            with self.subTest(arguments=arguments):
                response = server.handle({"jsonrpc": "2.0", "id": 11, "method": "tools/call",
                    "params": {"name": "blueprint_graph_edit", "arguments": arguments}})
                self.assertEqual(response["error"]["code"], -32602)

    def test_logic_unit_replace_schema_is_exact_and_bounded(self):
        server = MCPServer(FakeBridge(), writable=True)
        base = {
            "operation_id": "a" * 32,
            "asset_path": "/Game/Actors/BP_Light.BP_Light",
            "expected_snapshot": "b" * 40,
            "function_id": "c" * 32,
            "expected_function_fingerprint": "d" * 40,
            "entry_node_id": "e" * 32,
            "result_node_id": "f" * 32,
            "owned_node_ids": ["1" * 32],
            "local_variable_ids": ["2" * 32],
            "entry_position": {"x": -320, "y": 0},
            "result_position": {"x": 640, "y": 0},
            "nodes": [],
            "pin_defaults": [],
            "connections": [],
        }
        macro = {
            "operation_id": "a" * 32,
            "asset_path": "/Game/Actors/BP_Light.BP_Light",
            "expected_snapshot": "b" * 40,
            "target_kind": "macro",
            "logic_unit_id": "c" * 32,
            "graph_id": "c" * 32,
            "expected_logic_unit_fingerprint": "d" * 40,
            "entry_node_id": "e" * 32,
            "result_node_id": "f" * 32,
            "owned_node_ids": ["1" * 32],
            "local_variable_ids": [],
            "entry_position": {"x": -320, "y": 0},
            "result_position": {"x": 640, "y": 0},
            "nodes": [],
            "pin_defaults": [],
            "connections": [],
            "external_connections": [],
        }
        handler = {
            "operation_id": "a" * 32,
            "asset_path": "/Game/Actors/BP_Light.BP_Light",
            "expected_snapshot": "b" * 40,
            "target_kind": "custom_event",
            "logic_unit_id": "e" * 32,
            "graph_id": "c" * 32,
            "expected_logic_unit_fingerprint": "d" * 40,
            "entry_node_id": "e" * 32,
            "owned_node_ids": ["1" * 32],
            "local_variable_ids": [],
            "entry_position": {"x": -320, "y": 0},
            "nodes": [{"key": "body", "action_id": "3" * 32,
                       "position": {"x": 0, "y": 0}}],
            "pin_defaults": [],
            "connections": [{
                "from": {"node_key": "$entry", "pin_name": "then"},
                "to": {"node_key": "body", "pin_name": "execute"},
            }],
            "external_connections": [{
                "from": {"node_id": "4" * 32, "pin_id": "5" * 32},
                "to": {"node_key": "body", "pin_name": "Value"},
            }],
        }
        layout_macro = {
            **{key: value for key, value in macro.items()
               if key not in {"entry_position", "result_position"}},
            "layout": {"policy": "layered_v1"},
            "nodes": [{"key": "body", "action_id": "3" * 32}],
            "connections": [{
                "from": {"node_key": "$entry", "pin_name": "then"},
                "to": {"node_key": "body", "pin_name": "execute"},
                "automatic_conversion": True,
            }],
        }
        layout_handler = {
            **{key: value for key, value in handler.items() if key != "entry_position"},
            "layout": {"policy": "layered_v1"},
            "nodes": [{"key": "body", "action_id": "3" * 32}],
        }
        valid = (
            base,
            macro,
            handler,
            {**handler, "target_kind": "event"},
            layout_macro,
            layout_handler,
            {**layout_handler, "target_kind": "event"},
            {
                **base,
                "nodes": [{
                    "key": "branch",
                    "action_id": "3" * 32,
                    "position": {"x": 0, "y": 0},
                }],
                "pin_defaults": [{
                    "endpoint": {"node_key": "branch", "pin_name": "Condition"},
                    "value": {"kind": "literal", "value": True},
                }],
                "connections": [
                    {
                        "from": {"node_key": "$entry", "pin_name": "then"},
                        "to": {"node_key": "branch", "pin_name": "execute"},
                    },
                    {
                        "from": {"node_key": "branch", "pin_name": "else"},
                        "to": {"node_key": "$result", "pin_name": "execute"},
                        "automatic_conversion": True,
                        "conversion_position": {"x": 320, "y": 160},
                    },
                ],
            },
        )
        for arguments in valid:
            with self.subTest(arguments=arguments):
                response = server.handle({
                    "jsonrpc": "2.0", "id": 12, "method": "tools/call",
                    "params": {"name": "blueprint_block_replace", "arguments": arguments},
                })
                self.assertNotIn("error", response)
        invalid = (
            {},
            {**base, "expected_function_fingerprint": "short"},
            {**base, "entry_position": {"x": 1000001, "y": 0}},
            {**base, "nodes": [{"key": "$unsafe", "action_id": "3" * 32,
                               "position": {"x": 0, "y": 0}}]},
            {**base, "connections": [{
                "from": {"node_key": "$entry", "pin_name": "then"},
                "to": {"node_key": "$result", "pin_name": "execute"},
                "automatic_conversion": True,
            }]},
            {**base, "force": True},
            {**macro, "external_connections": [{
                "from": {"node_id": "4" * 32, "pin_id": "5" * 32},
                "to": {"node_key": "$entry", "pin_name": "Value"},
            }]},
            {**handler, "result_node_id": "f" * 32},
            {**handler, "local_variable_ids": ["2" * 32]},
            {**layout_macro, "entry_position": {"x": 0, "y": 0}},
            {**layout_macro, "layout": {"policy": "unknown"}},
            {**layout_macro, "nodes": [{"key": "body", "action_id": "3" * 32,
                                         "position": {"x": 0, "y": 0}}]},
            {**layout_macro, "connections": [{
                "from": {"node_key": "$entry", "pin_name": "then"},
                "to": {"node_key": "body", "pin_name": "execute"},
                "automatic_conversion": True,
                "conversion_position": {"x": 0, "y": 0},
            }]},
            {**handler, "external_connections": [{
                "from": {"node_id": "4" * 32, "pin_id": "5" * 32},
                "to": {"node_id": "6" * 32, "pin_id": "7" * 32},
            }]},
        )
        for arguments in invalid:
            with self.subTest(arguments=arguments):
                response = server.handle({
                    "jsonrpc": "2.0", "id": 13, "method": "tools/call",
                    "params": {"name": "blueprint_block_replace", "arguments": arguments},
                })
                self.assertEqual(response["error"]["code"], -32602)

    def test_domain_error_is_tool_error(self):
        class ErrorBridge(FakeBridge):
            def call(self, command, arguments=None):
                raise BridgeError("offline", code=ErrorCode.EDITOR_UNAVAILABLE, retryable=True)
        response = MCPServer(ErrorBridge()).handle({"jsonrpc": "2.0", "id": 1, "method": "tools/call", "params": {"name": "editor_state", "arguments": {}}})
        self.assertTrue(response["result"]["isError"])
        payload = json.loads(response["result"]["content"][0]["text"])
        self.assertEqual(payload["code"], "editor_unavailable")

    def test_capabilities_returns_local_project_identity_while_editor_is_unavailable(self):
        class OfflineBridge(FakeBridge):
            def call(self, command, arguments=None):
                raise BridgeError("offline", code=ErrorCode.EDITOR_UNAVAILABLE, retryable=True)

        identity = ProjectIdentity("Space Project", "f" * 40)
        for writable in (False, True):
            for lifecycle_enabled in (False, True):
                with self.subTest(writable=writable, lifecycle_enabled=lifecycle_enabled):
                    lifecycle = FakeLifecycle() if lifecycle_enabled else None
                    response = MCPServer(
                        OfflineBridge(), project_identity=identity,
                        writable=writable, lifecycle=lifecycle,
                    ).handle({
                        "jsonrpc": "2.0", "id": 1, "method": "tools/call",
                        "params": {"name": "capabilities", "arguments": {}},
                    })
                    self.assertFalse(response["result"].get("isError", False))
                    payload = json.loads(response["result"]["content"][0]["text"])
                    self.assertEqual(payload["project_name"], "Space Project")
                    self.assertEqual(payload["project_hash"], "f" * 40)
                    self.assertFalse(payload["bridge_ready"])
                    self.assertFalse(payload["native_capabilities_available"])
                    self.assertEqual(
                        payload["access_mode"],
                        "writable" if writable else "readonly",
                    )
                    self.assertEqual(payload["editor_lifecycle"]["enabled"], lifecycle_enabled)
                    self.assertNotIn("version_match", payload)
                    self.assertNotIn("bridge_version", payload)
                    self.assertNotIn("commands", payload)

    def test_capabilities_preserves_non_availability_errors(self):
        class InvalidBridge(FakeBridge):
            def call(self, command, arguments=None):
                raise BridgeError("bad configuration", code=ErrorCode.INVALID_CONFIGURATION)

        response = MCPServer(
            InvalidBridge(),
            project_identity=ProjectIdentity("Example", "e" * 40),
        ).handle({
            "jsonrpc": "2.0", "id": 1, "method": "tools/call",
            "params": {"name": "capabilities", "arguments": {}},
        })
        self.assertTrue(response["result"]["isError"])
        payload = json.loads(response["result"]["content"][0]["text"])
        self.assertEqual(payload["code"], "invalid_configuration")

    def test_phase_seventeen_game_data_schemas_are_exact_and_bounded(self):
        server = MCPServer(FakeBridge(), writable=True)
        operation_id = "a" * 32
        snapshot = "b" * 40
        member = {"name": "Damage", "type": {"category": "int", "container": "none"},
                  "default": {"kind": "literal", "value": 25}}
        valid = (
            ("game_data_inspect", {"target": "user_defined_struct", "asset_path": "/Game/Data/ST_Weapon.ST_Weapon"}),
            ("game_data_inspect", {"target": "data_table", "asset_path": "/Game/Data/DT_Weapons.DT_Weapons",
                                   "row_names": ["Pistol", "Rifle"], "page_size": 10}),
            ("game_data_inspect", {"cursor": "c" * 32, "page_size": 50}),
            ("game_data_edit", {"operation_id": operation_id, "target": "user_defined_struct", "operation": "create",
                                "asset_path": "/Game/Data/ST_Weapon", "members": [member]}),
            ("game_data_edit", {"operation_id": operation_id, "target": "user_defined_struct", "operation": "add_member",
                                "asset_path": "/Game/Data/ST_Weapon.ST_Weapon", "expected_snapshot": snapshot, "member": member}),
            ("game_data_edit", {"operation_id": operation_id, "target": "user_defined_struct", "operation": "reorder_member",
                                "asset_path": "/Game/Data/ST_Weapon.ST_Weapon", "expected_snapshot": snapshot,
                                "member_id": "c" * 32, "relative_to_member_id": "d" * 32, "position": "above"}),
            ("game_data_edit", {"operation_id": operation_id, "target": "data_table", "operation": "create",
                                "asset_path": "/Game/Data/DT_Weapons", "row_struct": "/Game/Data/ST_Weapon.ST_Weapon",
                                "rows": [{"row_name": "Rifle", "values": {"Damage": 42, "Tags": ["primary"],
                                    "Tuning": {"kind": "struct", "fields": {"Scale": 1.5}},
                                    "Lookup": {"kind": "map", "entries": [{"key": "body", "value": 1.0}]}}}]}),
            ("game_data_edit", {"operation_id": operation_id, "target": "data_table", "operation": "replace_row",
                                "asset_path": "/Game/Data/DT_Weapons.DT_Weapons", "expected_snapshot": snapshot,
                                "row_name": "Rifle", "values": {"Damage": 45}, "preserve_unspecified": True}),
            ("game_data_edit", {"operation_id": operation_id, "target": "data_table", "operation": "batch",
                                "asset_path": "/Game/Data/DT_Weapons.DT_Weapons", "expected_snapshot": snapshot,
                                "upserts": [{"row_name": "Pistol", "values": {"Damage": 30}}], "remove_rows": ["Old"]}),
        )
        for name, arguments in valid:
            with self.subTest(name=name, arguments=arguments):
                response = server.handle({"jsonrpc": "2.0", "id": 17, "method": "tools/call",
                                          "params": {"name": name, "arguments": arguments}})
                self.assertNotIn("error", response)
        invalid = (
            ("game_data_inspect", {"target": "user_defined_struct", "asset_path": "/Game/ST.ST", "row_names": ["x"]}),
            ("game_data_inspect", {"target": "data_table", "asset_path": "/Game/DT.DT", "page_size": 101}),
            ("game_data_edit", {"operation_id": operation_id, "target": "user_defined_struct", "operation": "create",
                                "asset_path": "/Game/ST", "members": []}),
            ("game_data_edit", {"operation_id": operation_id, "target": "data_table", "operation": "add_row",
                                "asset_path": "/Game/DT.DT", "expected_snapshot": snapshot, "row_name": "Rifle",
                                "values": {"Unsafe": {"kind": "raw", "value": "(X=1)"}}}),
            ("game_data_edit", {"operation_id": operation_id, "target": "data_table", "operation": "batch",
                                "asset_path": "/Game/DT.DT", "expected_snapshot": snapshot,
                                "upserts": [{"row_name": str(index), "values": {}} for index in range(65)], "remove_rows": []}),
        )
        for name, arguments in invalid:
            with self.subTest(name=name, arguments=arguments):
                response = server.handle({"jsonrpc": "2.0", "id": 18, "method": "tools/call",
                                          "params": {"name": name, "arguments": arguments}})
                self.assertEqual(response["error"]["code"], -32602)

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
