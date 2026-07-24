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


if __name__ == "__main__":
    unittest.main()
