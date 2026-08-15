import contextlib
import importlib.util
import io
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "package_plugin.py"
SPEC = importlib.util.spec_from_file_location("package_plugin", SCRIPT)
package_plugin = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(package_plugin)


class PackagePluginScriptTests(unittest.TestCase):
    def test_entrypoint_reexports_the_packaging_service(self):
        from scripts import packaging

        self.assertIs(package_plugin.main, packaging.main)
        self.assertIs(package_plugin.prepare_package, packaging.prepare_package)
        self.assertTrue(hasattr(packaging, "PackageRequest"))

    def write_engine(self, folder: Path, major: int = 5, minor: int = 8) -> None:
        batch_files = folder / "Engine" / "Build" / "BatchFiles"
        batch_files.mkdir(parents=True)
        shell_launcher = batch_files / "RunUAT.sh"
        shell_launcher.write_text("#!/bin/sh\n", encoding="utf-8")
        shell_launcher.chmod(0o755)
        (batch_files / "RunUAT.bat").write_text("@echo off\r\n", encoding="utf-8")
        (folder / "Engine" / "Build" / "Build.version").write_text(
            json.dumps({"MajorVersion": major, "MinorVersion": minor}),
            encoding="utf-8",
        )

    def test_main_uses_ue58_environment_default(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            engine_root = root / "UE_5.8"
            self.write_engine(engine_root)
            run_uat = engine_root / "Engine" / "Build" / "BatchFiles" / "RunUAT.bat"
            output = root / "Package"
            stdout = io.StringIO()

            with (
                mock.patch.object(package_plugin.platform, "system", return_value="Windows"),
                mock.patch.dict(os.environ, {"UE58": str(engine_root)}),
                contextlib.redirect_stdout(stdout),
            ):
                result = package_plugin.main(["--output", str(output), "--dry-run"])

            self.assertEqual(result, 0)
            self.assertIn(str(run_uat.resolve()), stdout.getvalue())
            self.assertIn("defaults to UE58", package_plugin.create_parser().format_help())
            self.assertIn("XCODE26_1_1/Contents/Developer", package_plugin.create_parser().format_help())

    def test_build_command_uses_fixed_plugin_and_output_arguments(self):
        run_uat = Path("/Engine/RunUAT.sh")
        output = Path("/Workspace With Spaces/build/unreal-editor-mcp")
        command = package_plugin.build_command(
            run_uat,
            output,
            "Mac",
            strict_includes=True,
            unversioned=False,
        )
        self.assertEqual(command[0:2], [str(run_uat), "BuildPlugin"])
        self.assertIn(f"-Plugin={package_plugin.PLUGIN_DESCRIPTOR}", command)
        self.assertIn(f"-Package={output}", command)
        self.assertIn("-TargetPlatforms=Mac", command)
        self.assertIn("-StrictIncludes", command)
        self.assertNotIn("-Unversioned", command)

    def test_default_output_is_workspace_build_directory(self):
        self.assertEqual(
            package_plugin.DEFAULT_OUTPUT,
            package_plugin.WORKSPACE_ROOT / "build" / "unreal-editor-mcp",
        )

    def test_fixture_build_uses_its_independent_descriptor(self):
        command = package_plugin.build_command(
            Path("/Engine/RunUAT.sh"),
            Path("/Workspace/build/unreal-mcp-test-companion"),
            "Win64",
            strict_includes=False,
            unversioned=False,
            plugin_descriptor=package_plugin.FIXTURE_DESCRIPTOR,
            dependency_plugins=(package_plugin.PLUGIN_DESCRIPTOR,),
        )
        self.assertIn(f"-Plugin={package_plugin.FIXTURE_DESCRIPTOR}", command)
        self.assertIn(f"-Dependencies={package_plugin.PLUGIN_DESCRIPTOR}", command)

    def test_gas_build_uses_its_independent_descriptor_and_base_dependency(self):
        command = package_plugin.build_command(
            Path("/Engine/RunUAT.sh"),
            Path("/Workspace/build/unreal-mcp-gas"),
            "Win64",
            strict_includes=True,
            unversioned=False,
            plugin_descriptor=package_plugin.GAS_DESCRIPTOR,
            dependency_plugins=(package_plugin.PLUGIN_DESCRIPTOR,),
        )
        self.assertIn(f"-Plugin={package_plugin.GAS_DESCRIPTOR}", command)
        self.assertIn(f"-Dependencies={package_plugin.PLUGIN_DESCRIPTOR}", command)
        self.assertIn("-StrictIncludes", command)

    def test_commonui_build_uses_its_independent_descriptor_and_base_dependency(self):
        command = package_plugin.build_command(
            Path("/Engine/RunUAT.sh"),
            Path("/Workspace/build/unreal-mcp-commonui"),
            "Win64",
            strict_includes=True,
            unversioned=False,
            plugin_descriptor=package_plugin.COMMONUI_DESCRIPTOR,
            dependency_plugins=(package_plugin.PLUGIN_DESCRIPTOR,),
        )
        self.assertIn(f"-Plugin={package_plugin.COMMONUI_DESCRIPTOR}", command)
        self.assertIn(f"-Dependencies={package_plugin.PLUGIN_DESCRIPTOR}", command)
        self.assertIn("-StrictIncludes", command)

    def test_enhanced_input_build_uses_its_independent_descriptor_and_base_dependency(self):
        command = package_plugin.build_command(
            Path("/Engine/RunUAT.sh"),
            Path("/Workspace/build/unreal-mcp-enhanced-input"),
            "Win64",
            strict_includes=True,
            unversioned=False,
            plugin_descriptor=package_plugin.ENHANCED_INPUT_DESCRIPTOR,
            dependency_plugins=(package_plugin.PLUGIN_DESCRIPTOR,),
        )
        self.assertIn(f"-Plugin={package_plugin.ENHANCED_INPUT_DESCRIPTOR}", command)
        self.assertIn(f"-Dependencies={package_plugin.PLUGIN_DESCRIPTOR}", command)
        self.assertIn("-StrictIncludes", command)

    def test_engine_validation_selects_the_platform_launcher(self):
        with tempfile.TemporaryDirectory() as temporary:
            engine_root = Path(temporary)
            self.write_engine(engine_root)
            batch_files = engine_root / "Engine" / "Build" / "BatchFiles"
            shell_launcher = batch_files / "RunUAT.sh"
            batch_launcher = batch_files / "RunUAT.bat"

            self.assertEqual(
                package_plugin.validate_engine_root(engine_root, "Darwin"), shell_launcher.resolve()
            )
            self.assertEqual(
                package_plugin.validate_engine_root(engine_root, "Linux"), shell_launcher.resolve()
            )
            self.assertEqual(
                package_plugin.validate_engine_root(engine_root, "Windows"), batch_launcher.resolve()
            )

    def test_engine_validation_rejects_unsupported_or_invalid_version(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            unsupported = root / "UE_5.7"
            self.write_engine(unsupported, 5, 7)
            with self.assertRaisesRegex(package_plugin.PackagingError, "5.8 or newer"):
                package_plugin.validate_engine_root(unsupported, "Windows")

            invalid = root / "UE_Invalid"
            self.write_engine(invalid)
            (invalid / "Engine" / "Build" / "Build.version").write_text(
                json.dumps({"MajorVersion": True, "MinorVersion": 8}),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(package_plugin.PackagingError, "invalid major/minor"):
                package_plugin.validate_engine_root(invalid, "Windows")

    def test_environment_validates_macos_xcode_and_skips_it_elsewhere(self):
        with tempfile.TemporaryDirectory() as temporary:
            developer_dir = Path(temporary) / "Xcode.app" / "Contents" / "Developer"
            xcodebuild = developer_dir / "usr" / "bin" / "xcodebuild"
            xcodebuild.parent.mkdir(parents=True)
            xcodebuild.write_bytes(b"")

            environment = package_plugin.configure_environment("Darwin", developer_dir)
            self.assertEqual(environment["DEVELOPER_DIR"], str(developer_dir.resolve()))
            package_plugin.configure_environment("Windows", None)

    def test_environment_derives_developer_directory_from_xcode_app(self):
        with tempfile.TemporaryDirectory() as temporary:
            xcode_app = Path(temporary) / "Xcode.app"
            developer_dir = xcode_app / "Contents" / "Developer"
            xcodebuild = developer_dir / "usr" / "bin" / "xcodebuild"
            xcodebuild.parent.mkdir(parents=True)
            xcodebuild.write_bytes(b"")

            with mock.patch.dict(
                os.environ,
                {
                    "XCODE26_1_1": str(xcode_app),
                    "DEVELOPER_DIR": "/unexpected/global/developer/directory",
                },
                clear=True,
            ):
                environment = package_plugin.configure_environment("Darwin", None)

            self.assertEqual(environment["DEVELOPER_DIR"], str(developer_dir.resolve()))

    def test_environment_rejects_old_developer_directory_variable(self):
        with tempfile.TemporaryDirectory() as temporary:
            developer_dir = Path(temporary)
            with (
                mock.patch.dict(
                    os.environ,
                    {"UNREAL_MCP_DEVELOPER_DIR": str(developer_dir)},
                    clear=True,
                ),
                self.assertRaisesRegex(package_plugin.PackagingError, "XCODE26_1_1"),
            ):
                package_plugin.configure_environment("Darwin", None)

    def test_environment_rejects_xcode_app_without_xcodebuild(self):
        with tempfile.TemporaryDirectory() as temporary:
            xcode_app = Path(temporary) / "Xcode.app"
            xcode_app.mkdir()
            with (
                mock.patch.dict(
                    os.environ,
                    {"XCODE26_1_1": str(xcode_app)},
                    clear=True,
                ),
                self.assertRaisesRegex(
                    package_plugin.PackagingError,
                    "does not point to an Xcode app",
                ),
            ):
                package_plugin.configure_environment("Darwin", None)

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
            descriptor = json.loads(
                package_plugin.PLUGIN_DESCRIPTOR.read_text(encoding="utf-8")
            )
            descriptor["Installed"] = True
            (output / package_plugin.PLUGIN_DESCRIPTOR.name).write_text(
                json.dumps(descriptor), encoding="utf-8"
            )
            binary = output / "Binaries" / "Mac" / "UnrealEditor-UnrealMCP.dylib"
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b"binary")
            package_plugin.verify_package(output)

    def test_descriptor_restoration_preserves_uat_fields_and_source_contract(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            packaged_path = output / package_plugin.GAS_DESCRIPTOR.name
            packaged_path.write_text(
                json.dumps({"Installed": True, "EngineVersion": "5.8.0"}),
                encoding="utf-8",
            )

            package_plugin.restore_source_descriptor_contract(
                output, package_plugin.GAS_DESCRIPTOR
            )

            packaged = json.loads(packaged_path.read_text(encoding="utf-8"))
            source = json.loads(package_plugin.GAS_DESCRIPTOR.read_text(encoding="utf-8"))
            for field, value in source.items():
                if field not in package_plugin.PACKAGING_OWNED_DESCRIPTOR_FIELDS:
                    self.assertEqual(packaged[field], value)
            self.assertTrue(packaged["Installed"])
            self.assertEqual(packaged["EngineVersion"], "5.8.0")

    def test_package_verification_rejects_stripped_descriptor_contract(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            descriptor = json.loads(
                package_plugin.GAS_DESCRIPTOR.read_text(encoding="utf-8")
            )
            descriptor["Installed"] = True
            descriptor.pop("companion_api_version")
            (output / package_plugin.GAS_DESCRIPTOR.name).write_text(
                json.dumps(descriptor), encoding="utf-8"
            )
            binary = output / "Binaries" / "Win64" / "UnrealEditor-UnrealMCPGAS.dll"
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b"binary")

            with self.assertRaisesRegex(
                package_plugin.PackagingError, "companion_api_version"
            ):
                package_plugin.verify_package(output, package_plugin.GAS_DESCRIPTOR)

    def test_descriptor_reader_rejects_oversized_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            descriptor = Path(temporary) / "Oversized.uplugin"
            descriptor.write_bytes(b" " * (package_plugin.MAX_PLUGIN_DESCRIPTOR_BYTES + 1))
            with self.assertRaisesRegex(package_plugin.PackagingError, "larger than 1 MiB"):
                package_plugin.read_plugin_descriptor(descriptor)

    def test_package_verification_rejects_non_installed_or_binary_free_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            descriptor = output / package_plugin.PLUGIN_DESCRIPTOR.name
            value = json.loads(
                package_plugin.PLUGIN_DESCRIPTOR.read_text(encoding="utf-8")
            )
            value["Installed"] = False
            descriptor.write_text(json.dumps(value), encoding="utf-8")
            with self.assertRaises(package_plugin.PackagingError):
                package_plugin.verify_package(output)

            value["Installed"] = True
            descriptor.write_text(json.dumps(value), encoding="utf-8")
            with self.assertRaises(package_plugin.PackagingError):
                package_plugin.verify_package(output)


if __name__ == "__main__":
    unittest.main()
