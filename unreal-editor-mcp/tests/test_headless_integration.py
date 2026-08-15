import importlib.util
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "run_headless_integration.py"
SPEC = importlib.util.spec_from_file_location("run_headless_integration", SCRIPT)
run_headless_integration = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(run_headless_integration)


class HeadlessIntegrationScriptTests(unittest.TestCase):
    def test_runner_is_a_thin_compatibility_orchestrator(self):
        from headless_integration import (
            assets,
            blueprint_declarations,
            blueprint_fixture_preparation,
            blueprint_graph_editing,
            blueprint_restart_verification,
            blueprint_state,
            blueprints,
            capability_contract,
            game_data_levels,
            lifecycle,
            process_lifecycle,
            widgets,
        )

        self.assertIs(run_headless_integration.main, lifecycle.main)
        self.assertIs(
            run_headless_integration.resolve_editor_executable,
            process_lifecycle.resolve_editor_executable,
        )
        self.assertIs(lifecycle.resolve_editor_executable, process_lifecycle.resolve_editor_executable)
        self.assertTrue(callable(assets.run_asset_scenario))
        self.assertTrue(callable(blueprint_declarations.author_blueprint_declarations))
        self.assertTrue(callable(blueprints.prepare_blueprint_scenario))
        self.assertTrue(callable(blueprints.author_blueprint_scenario))
        self.assertTrue(callable(blueprints.verify_restarted_blueprints))
        self.assertTrue(issubclass(blueprint_state.BlueprintScenarioState, dict) is False)
        self.assertTrue(callable(capability_contract.verify_capability_contract))
        self.assertTrue(callable(game_data_levels.open_acceptance_level))
        self.assertTrue(callable(widgets.author_widget_scenario))

    def test_runner_requests_ue58_and_disposable_project_environment_paths(self):
        from headless_integration import lifecycle

        with (
            patch.object(
                lifecycle,
                "required_path",
                side_effect=[Path("engine"), Path("project")],
            ) as required_path,
            patch.object(
                lifecycle,
                "resolve_editor_executable",
                side_effect=RuntimeError("stop after environment lookup"),
            ),
            self.assertRaisesRegex(RuntimeError, "stop after environment lookup"),
        ):
            lifecycle.main()

        self.assertEqual(
            required_path.call_args_list,
            [
                unittest.mock.call("UE58"),
                unittest.mock.call("UNREAL_MCP_TEST_UPROJECT"),
            ],
        )

    def test_editor_executable_is_selected_for_each_supported_host(self):
        expected_paths = {
            "Darwin": Path("Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"),
            "Windows": Path("Engine/Binaries/Win64/UnrealEditor-Cmd.exe"),
            "Linux": Path("Engine/Binaries/Linux/UnrealEditor"),
        }
        with tempfile.TemporaryDirectory() as temporary:
            engine = Path(temporary)
            for relative in expected_paths.values():
                executable = engine / relative
                executable.parent.mkdir(parents=True, exist_ok=True)
                executable.write_bytes(b"")

            for host_system, relative in expected_paths.items():
                with self.subTest(host_system=host_system):
                    self.assertEqual(
                        run_headless_integration.resolve_editor_executable(engine, host_system),
                        engine / relative,
                    )

    def test_editor_executable_rejects_missing_and_unknown_hosts(self):
        with tempfile.TemporaryDirectory() as temporary:
            engine = Path(temporary)
            with self.assertRaises(SystemExit):
                run_headless_integration.resolve_editor_executable(engine, "Windows")
            with self.assertRaises(SystemExit):
                run_headless_integration.resolve_editor_executable(engine, "Plan9")

    def test_xcode_app_is_required_only_on_macos(self):
        with patch.dict(os.environ, {}, clear=True):
            windows_environment = run_headless_integration.configure_editor_environment("Windows")
            linux_environment = run_headless_integration.configure_editor_environment("Linux")
            self.assertNotIn("DEVELOPER_DIR", windows_environment)
            self.assertNotIn("DEVELOPER_DIR", linux_environment)
            with self.assertRaises(SystemExit):
                run_headless_integration.configure_editor_environment("Darwin")

        with tempfile.TemporaryDirectory() as temporary:
            xcode_app = Path(temporary) / "Xcode.app"
            developer = xcode_app / "Contents" / "Developer"
            xcodebuild = developer / "usr" / "bin" / "xcodebuild"
            xcodebuild.parent.mkdir(parents=True)
            xcodebuild.write_bytes(b"")
            with patch.dict(
                os.environ,
                {"XCODE26_1_1": str(xcode_app)},
                clear=True,
            ):
                environment = run_headless_integration.configure_editor_environment("Darwin")
            self.assertEqual(environment["DEVELOPER_DIR"], str(developer.resolve()))

    def test_old_or_inherited_developer_directory_does_not_replace_xcode_app(self):
        with tempfile.TemporaryDirectory() as temporary:
            developer = Path(temporary)
            for variable in ("UNREAL_MCP_DEVELOPER_DIR", "DEVELOPER_DIR"):
                with (
                    self.subTest(variable=variable),
                    patch.dict(os.environ, {variable: str(developer)}, clear=True),
                    self.assertRaisesRegex(SystemExit, "XCODE26_1_1"),
                ):
                    run_headless_integration.configure_editor_environment("Darwin")

    def test_readonly_acceptance_exercises_every_tool_and_ignores_generated_state(self):
        from headless_integration.readonly_mode import READONLY_TOOL_NAMES, verify_readonly_mode
        from unreal_editor_mcp import __version__
        from unreal_editor_mcp.project import ProjectLayout

        class Bridge:
            def __init__(self, layout):
                self.layout = layout
                self.calls = []

            def call(self, command, arguments=None):
                self.calls.append(command)
                self.layout.state_dir.mkdir(parents=True, exist_ok=True)
                (self.layout.state_dir / "allowed.json").write_text("generated", encoding="utf-8")
                if command == "capabilities":
                    return {"bridge_version": __version__, "bridge_instance_id": "b" * 32}
                if command == "asset_inspect":
                    if arguments and arguments.get("asset_path") == "/Game/DT_Fixture.DT_Fixture":
                        if arguments.get("selector") == "columns/Damage":
                            return {"name": "Damage", "values": []}
                        return {"type": "data_table", "schema": {"field_count": 2}}
                    return {
                        "snapshot_id": "c" * 40,
                        "graph": {"debug": {"graph_guid": "d" * 32}},
                    }
                return {}

            def close(self):
                pass

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            descriptor = root / "ReadonlyFixture.uproject"
            descriptor.write_text("{}", encoding="utf-8")
            content = root / "Content"
            content.mkdir()
            (content / "Fixture.uasset").write_bytes(b"unchanged")
            layout = ProjectLayout.resolve(descriptor)
            bridge = Bridge(layout)
            verify_readonly_mode(
                bridge,
                layout,
                bridge_instance_id="b" * 32,
                blueprint_path="/Game/BP_Fixture.BP_Fixture",
                game_data_path="/Game/DT_Fixture.DT_Fixture",
                map_path="/Game/L_Fixture.L_Fixture",
            )
            self.assertEqual(set(bridge.calls), set(READONLY_TOOL_NAMES))

    def test_readonly_acceptance_detects_project_content_changes(self):
        from headless_integration.readonly_mode import verify_readonly_mode
        from unreal_editor_mcp import __version__
        from unreal_editor_mcp.project import ProjectLayout

        class Bridge:
            def __init__(self, layout):
                self.layout = layout
                self.changed = False

            def call(self, command, arguments=None):
                if not self.changed:
                    (self.layout.root / "Config" / "DefaultGame.ini").write_text("changed", encoding="utf-8")
                    self.changed = True
                if command == "capabilities":
                    return {"bridge_version": __version__, "bridge_instance_id": "b" * 32}
                if command == "asset_inspect":
                    if arguments and arguments.get("asset_path") == "/Game/DT_Fixture.DT_Fixture":
                        if arguments.get("selector") == "columns/Damage":
                            return {"name": "Damage", "values": []}
                        return {"type": "data_table", "schema": {"field_count": 2}}
                    return {
                        "snapshot_id": "c" * 40,
                        "graph": {"debug": {"graph_guid": "d" * 32}},
                    }
                return {}

            def close(self):
                pass

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            descriptor = root / "ReadonlyFixture.uproject"
            descriptor.write_text("{}", encoding="utf-8")
            config = root / "Config"
            config.mkdir()
            (config / "DefaultGame.ini").write_text("original", encoding="utf-8")
            layout = ProjectLayout.resolve(descriptor)
            with self.assertRaisesRegex(AssertionError, "changed project-owned files"):
                verify_readonly_mode(
                    Bridge(layout),
                    layout,
                    bridge_instance_id="b" * 32,
                    blueprint_path="/Game/BP_Fixture.BP_Fixture",
                    game_data_path="/Game/DT_Fixture.DT_Fixture",
                    map_path="/Game/L_Fixture.L_Fixture",
                )

    def test_lifecycle_only_acceptance_covers_catalog_rejection_and_restart(self):
        from headless_integration.readonly_mode import (
            READONLY_LIFECYCLE_TOOL_NAMES,
            _RecordingBridge,
            verify_readonly_lifecycle_server,
        )
        from unreal_editor_mcp import __version__
        from unreal_editor_mcp.project import ProjectLayout
        from unreal_editor_mcp.server import MCPServer

        class Bridge:
            def __init__(self):
                self.instance = "a" * 32

            def call(self, command, arguments=None):
                if command == "capabilities":
                    return {
                        "bridge_version": __version__,
                        "bridge_instance_id": self.instance,
                    }
                return {}

            def close(self):
                pass

        class Lifecycle:
            def __init__(self, bridge):
                self.bridge = bridge

            def availability(self):
                return {"enabled": True, "launch_configured": True}

            def execute(self, arguments):
                operation = arguments["operation"]
                if operation == "launch":
                    return {"state": "ready", "new_bridge_instance_id": self.bridge.instance}
                if operation == "restart":
                    old = self.bridge.instance
                    self.bridge.instance = "b" * 32
                    return {
                        "state": "ready",
                        "old_bridge_instance_id": old,
                        "new_bridge_instance_id": self.bridge.instance,
                    }
                return {"state": "stopped"}

            def close(self):
                pass

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            descriptor = root / "ReadonlyLifecycle.uproject"
            descriptor.write_text("{}", encoding="utf-8")
            content = root / "Content"
            content.mkdir()
            (content / "Fixture.uasset").write_bytes(b"unchanged")
            layout = ProjectLayout.resolve(descriptor)
            bridge = Bridge()
            recording = _RecordingBridge(bridge)
            server = MCPServer(
                recording,
                project_identity=layout.identity(),
                lifecycle=Lifecycle(bridge),
            )
            verify_readonly_lifecycle_server(server, recording, layout)
            names = [tool["name"] for tool in server.tools]
            self.assertEqual(names, READONLY_LIFECYCLE_TOOL_NAMES)
            self.assertTrue(recording.calls)
            self.assertEqual(set(recording.calls), {"capabilities"})

    def test_lost_operation_reconciliation_accepts_terminal_partial_state(self):
        from headless_integration.operations import reconcile_operation

        class Bridge:
            def call(self, command, arguments=None):
                self.command = command
                self.arguments = arguments
                return {"state": "partial", "retained": True}

        bridge = Bridge()
        result = reconcile_operation(bridge, "a" * 32, "b" * 32)
        self.assertEqual(result["state"], "partial")
        self.assertEqual(bridge.command, "operation_status")

    def test_cursor_collection_is_bounded_and_rejects_repeated_cursors(self):
        from headless_integration.pagination import collect_cursor_pages

        with self.assertRaisesRegex(AssertionError, "repeated cursor"):
            collect_cursor_pages(
                {"records": [1], "next_cursor": "same"},
                lambda cursor: {"records": [2], "next_cursor": cursor},
            )


if __name__ == "__main__":
    unittest.main()
