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
            blueprints,
            game_data_levels,
            lifecycle,
            widgets,
        )

        self.assertIs(run_headless_integration.main, lifecycle.main)
        self.assertIs(
            run_headless_integration.resolve_editor_executable,
            lifecycle.resolve_editor_executable,
        )
        self.assertTrue(callable(assets.run_asset_scenario))
        self.assertTrue(callable(blueprint_declarations.author_blueprint_declarations))
        self.assertIs(
            blueprints.prepare_blueprint_scenario,
            blueprint_fixture_preparation.prepare_blueprint_scenario,
        )
        self.assertIs(
            blueprints.author_blueprint_scenario,
            blueprint_graph_editing.author_blueprint_scenario,
        )
        self.assertIs(
            blueprints.verify_restarted_blueprints,
            blueprint_restart_verification.verify_restarted_blueprints,
        )
        self.assertTrue(callable(game_data_levels.open_acceptance_level))
        self.assertTrue(callable(widgets.author_widget_scenario))

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

    def test_developer_directory_is_required_only_on_macos(self):
        with patch.dict(os.environ, {}, clear=True):
            windows_environment = run_headless_integration.configure_editor_environment("Windows")
            linux_environment = run_headless_integration.configure_editor_environment("Linux")
            self.assertNotIn("DEVELOPER_DIR", windows_environment)
            self.assertNotIn("DEVELOPER_DIR", linux_environment)
            with self.assertRaises(SystemExit):
                run_headless_integration.configure_editor_environment("Darwin")

        with tempfile.TemporaryDirectory() as temporary:
            developer = Path(temporary)
            with patch.dict(
                os.environ,
                {"UNREAL_MCP_DEVELOPER_DIR": str(developer)},
                clear=True,
            ):
                environment = run_headless_integration.configure_editor_environment("Darwin")
            self.assertEqual(environment["DEVELOPER_DIR"], str(developer.resolve()))

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
                if command == "blueprint_inspect":
                    return {
                        "snapshot_id": "c" * 40,
                        "records": [{"section": "graph", "id": "d" * 32}],
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
                if command == "blueprint_inspect":
                    return {
                        "snapshot_id": "c" * 40,
                        "records": [{"section": "graph", "id": "d" * 32}],
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


if __name__ == "__main__":
    unittest.main()
