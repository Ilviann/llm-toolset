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
    def write_engine_version(self, engine_root: Path, minor: int = 7) -> None:
        version_file = engine_root / "Engine" / "Build" / "Build.version"
        version_file.parent.mkdir(parents=True, exist_ok=True)
        version_file.write_text(
            f'{{"MajorVersion": 5, "MinorVersion": {minor}}}', encoding="utf-8"
        )

    def test_main_uses_ue57_environment_default(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            engine_root = root / "UE_5.7"
            run_uat = engine_root / "Engine" / "Build" / "BatchFiles" / "RunUAT.bat"
            run_uat.parent.mkdir(parents=True)
            run_uat.write_text("@echo off\r\n", encoding="utf-8")
            self.write_engine_version(engine_root)
            output = root / "Package"
            stdout = io.StringIO()

            with (
                mock.patch.object(package_plugin.platform, "system", return_value="Windows"),
                mock.patch.dict(os.environ, {"UE57": str(engine_root)}),
                contextlib.redirect_stdout(stdout),
            ):
                result = package_plugin.main(["--output", str(output), "--dry-run"])

            self.assertEqual(result, 0)
            self.assertIn(str(run_uat.resolve()), stdout.getvalue())
            self.assertIn("defaults to UE57", package_plugin.create_parser().format_help())

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

    def test_ue57_dependency_staging_combines_modules_without_dependencies_argument(self):
        with package_plugin.prepare_plugin_build(
            package_plugin.GAS_DESCRIPTOR,
            (package_plugin.PLUGIN_DESCRIPTOR,),
        ) as prepared:
            descriptor = json.loads(prepared.descriptor.read_text(encoding="utf-8"))
            command = package_plugin.build_command(
                Path("/Engine/RunUAT.sh"),
                Path("/Workspace/build/unreal-mcp-gas"),
                "Win64",
                strict_includes=True,
                unversioned=False,
                plugin_descriptor=prepared.descriptor,
            )
            self.assertEqual(
                [module["Name"] for module in descriptor["Modules"]],
                ["UnrealMCPGAS", "UnrealMCP"],
            )
            self.assertNotIn("UnrealMCP", [plugin["Name"] for plugin in descriptor["Plugins"]])
            self.assertTrue((prepared.descriptor.parent / "Source/UnrealMCP").is_dir())
            self.assertIn(f"-Plugin={prepared.descriptor}", command)
            self.assertFalse(any(value.startswith("-Dependencies=") for value in command))
            self.assertIn("-StrictIncludes", command)

    def test_dependency_package_finalization_restores_companion_boundary(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            with package_plugin.prepare_plugin_build(
                package_plugin.GAS_DESCRIPTOR,
                (package_plugin.PLUGIN_DESCRIPTOR,),
            ) as prepared:
                descriptor = json.loads(prepared.descriptor.read_text(encoding="utf-8"))
                descriptor["Installed"] = True
                descriptor.pop("companion_api_version")
                descriptor.pop("unreal_mcp_companion")
                descriptor.pop("EnabledByDefault")
                (output / package_plugin.GAS_DESCRIPTOR.name).write_text(
                    json.dumps(descriptor), encoding="utf-8"
                )
                binaries = output / "Binaries" / "Win64"
                binaries.mkdir(parents=True)
                (binaries / "UnrealEditor-UnrealMCP.dll").write_bytes(b"base")
                (binaries / "UnrealEditor-UnrealMCPGAS.dll").write_bytes(b"gas")
                (binaries / "UnrealEditor.modules").write_text(
                    json.dumps(
                        {
                            "Modules": {
                                "UnrealMCP": "UnrealEditor-UnrealMCP.dll",
                                "UnrealMCPGAS": "UnrealEditor-UnrealMCPGAS.dll",
                            }
                        }
                    ),
                    encoding="utf-8",
                )
                source = output / "Source" / "UnrealMCP"
                source.mkdir(parents=True)
                (source / "UnrealMCP.Build.cs").write_text("", encoding="utf-8")

                package_plugin.finalize_dependency_package(output, prepared)
                package_plugin.restore_source_descriptor_contract(
                    output, package_plugin.GAS_DESCRIPTOR
                )

            finalized = json.loads(
                (output / package_plugin.GAS_DESCRIPTOR.name).read_text(encoding="utf-8")
            )
            self.assertEqual([module["Name"] for module in finalized["Modules"]], ["UnrealMCPGAS"])
            self.assertEqual(finalized["Modules"][0]["LoadingPhase"], "None")
            self.assertIn("UnrealMCP", [plugin["Name"] for plugin in finalized["Plugins"]])
            self.assertEqual(finalized["companion_api_version"], 1)
            self.assertEqual(
                finalized["unreal_mcp_companion"],
                json.loads(
                    package_plugin.GAS_DESCRIPTOR.read_text(encoding="utf-8")
                )["unreal_mcp_companion"],
            )
            self.assertFalse(finalized["EnabledByDefault"])
            self.assertTrue(finalized["Installed"])
            self.assertFalse((binaries / "UnrealEditor-UnrealMCP.dll").exists())
            self.assertTrue((binaries / "UnrealEditor-UnrealMCPGAS.dll").is_file())
            manifest = json.loads((binaries / "UnrealEditor.modules").read_text(encoding="utf-8"))
            self.assertEqual(list(manifest["Modules"]), ["UnrealMCPGAS"])
            self.assertFalse(source.exists())

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

    def test_engine_version_validation_accepts_only_5_7(self):
        with tempfile.TemporaryDirectory() as temporary:
            engine_root = Path(temporary)
            self.write_engine_version(engine_root)
            self.assertEqual(
                package_plugin.validate_supported_engine_version(engine_root), (5, 7)
            )
            for minor in (6, 8):
                with self.subTest(minor=minor):
                    self.write_engine_version(engine_root, minor)
                    with self.assertRaisesRegex(package_plugin.PackagingError, "5.7.x"):
                        package_plugin.validate_supported_engine_version(engine_root)

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
            engine_root = Path(temporary) / "UE_5.7"
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
