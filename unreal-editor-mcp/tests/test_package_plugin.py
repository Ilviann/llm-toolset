import importlib.util
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "package_plugin.py"
SPEC = importlib.util.spec_from_file_location("package_plugin", SCRIPT)
package_plugin = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(package_plugin)


class PackagePluginScriptTests(unittest.TestCase):
    def test_build_command_uses_fixed_plugin_and_output_arguments(self):
        command = package_plugin.build_command(
            Path("/Engine/RunUAT.sh"),
            Path("/Workspace With Spaces/build/unreal-editor-mcp"),
            "Mac",
            strict_includes=True,
            unversioned=False,
        )
        self.assertEqual(command[0:2], ["/Engine/RunUAT.sh", "BuildPlugin"])
        self.assertIn(f"-Plugin={package_plugin.PLUGIN_DESCRIPTOR}", command)
        self.assertIn("-Package=/Workspace With Spaces/build/unreal-editor-mcp", command)
        self.assertIn("-TargetPlatforms=Mac", command)
        self.assertIn("-StrictIncludes", command)
        self.assertNotIn("-Unversioned", command)

    def test_default_output_is_workspace_build_directory(self):
        self.assertEqual(
            package_plugin.DEFAULT_OUTPUT,
            package_plugin.WORKSPACE_ROOT / "build" / "unreal-editor-mcp",
        )

    def test_engine_validation_selects_the_platform_launcher(self):
        with tempfile.TemporaryDirectory() as temporary:
            engine_root = Path(temporary)
            batch_files = engine_root / "Engine" / "Build" / "BatchFiles"
            batch_files.mkdir(parents=True)
            shell_launcher = batch_files / "RunUAT.sh"
            shell_launcher.write_text("#!/bin/sh\n", encoding="utf-8")
            shell_launcher.chmod(0o755)
            batch_launcher = batch_files / "RunUAT.bat"
            batch_launcher.write_text("@echo off\r\n", encoding="utf-8")

            self.assertEqual(
                package_plugin.validate_engine_root(engine_root, "Darwin"), shell_launcher.resolve()
            )
            self.assertEqual(
                package_plugin.validate_engine_root(engine_root, "Linux"), shell_launcher.resolve()
            )
            self.assertEqual(
                package_plugin.validate_engine_root(engine_root, "Windows"), batch_launcher.resolve()
            )

    def test_environment_validates_macos_xcode_and_skips_it_elsewhere(self):
        with tempfile.TemporaryDirectory() as temporary:
            developer_dir = Path(temporary) / "Xcode.app" / "Contents" / "Developer"
            xcodebuild = developer_dir / "usr" / "bin" / "xcodebuild"
            xcodebuild.parent.mkdir(parents=True)
            xcodebuild.write_bytes(b"")

            environment = package_plugin.configure_environment("Darwin", developer_dir)
            self.assertEqual(environment["DEVELOPER_DIR"], str(developer_dir.resolve()))
            package_plugin.configure_environment("Windows", None)

    def test_target_platform_validation_rejects_duplicates_and_shell_text(self):
        self.assertEqual(package_plugin.normalize_target_platforms("Win64+Linux"), "Win64+Linux")
        for value in ("Mac+Mac", "Mac;rm", "Mac++Linux", ""):
            with self.subTest(value=value), self.assertRaises(package_plugin.PackagingError):
                package_plugin.normalize_target_platforms(value)

    def test_output_validation_rejects_protected_and_overlapping_directories(self):
        with tempfile.TemporaryDirectory() as temporary:
            engine_root = Path(temporary) / "UE_5.8"
            engine_root.mkdir()
            with self.assertRaises(package_plugin.PackagingError):
                package_plugin.validate_output(package_plugin.WORKSPACE_ROOT, engine_root)
            with self.assertRaises(package_plugin.PackagingError):
                package_plugin.validate_output(
                    package_plugin.PLUGIN_DESCRIPTOR.parent / "Package", engine_root
                )
            with self.assertRaises(package_plugin.PackagingError):
                package_plugin.validate_output(engine_root / "Package", engine_root)

    def test_package_verification_requires_installed_descriptor_and_binary(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            (output / package_plugin.PLUGIN_DESCRIPTOR.name).write_text(
                '{"Installed": true}', encoding="utf-8"
            )
            binary = output / "Binaries" / "Mac" / "UnrealEditor-UnrealMCP.dylib"
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b"binary")
            package_plugin.verify_package(output)

    def test_package_verification_rejects_non_installed_or_binary_free_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            descriptor = output / package_plugin.PLUGIN_DESCRIPTOR.name
            descriptor.write_text('{"Installed": false}', encoding="utf-8")
            with self.assertRaises(package_plugin.PackagingError):
                package_plugin.verify_package(output)

            descriptor.write_text('{"Installed": true}', encoding="utf-8")
            with self.assertRaises(package_plugin.PackagingError):
                package_plugin.verify_package(output)


if __name__ == "__main__":
    unittest.main()
