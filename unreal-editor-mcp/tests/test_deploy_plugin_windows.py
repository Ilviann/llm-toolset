import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from scripts import deploy_plugin_windows as deploy


class WindowsDeploymentScriptTests(unittest.TestCase):
    def write_project(self, folder: Path, association: object = "5.8") -> deploy.ProjectInfo:
        descriptor = folder / "Shooter.uproject"
        descriptor.write_text(
            json.dumps({"FileVersion": 3, "EngineAssociation": association}),
            encoding="utf-8",
        )
        return deploy.locate_project(folder)

    def write_package(self, folder: Path) -> None:
        (folder / "UnrealMCP.uplugin").write_text(
            json.dumps({"Installed": True}),
            encoding="utf-8",
        )
        binary = folder / "Binaries" / "Win64" / "UnrealEditor-UnrealMCP.dll"
        binary.parent.mkdir(parents=True)
        binary.write_bytes(b"binary")
        binary.with_suffix(".pdb").write_bytes(b"symbols")
        source = folder / "Source" / "UnrealMCP" / "Private" / "Module.cpp"
        source.parent.mkdir(parents=True)
        source.write_text("// source", encoding="utf-8")
        (folder / "Source" / "UnrealMCP" / "UnrealMCP.Build.cs").write_text(
            "public class UnrealMCP\n{\n    public UnrealMCP()\n    {\n"
            "        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;\n    }\n}\n",
            encoding="utf-8",
        )
        manifest = (
            folder
            / "Intermediate"
            / "Build"
            / "Win64"
            / "UnrealEditor"
            / "Development"
            / "UnrealMCP"
            / "UnrealMCP.precompiled"
        )
        manifest.parent.mkdir(parents=True)
        manifest.write_text("manifest", encoding="utf-8")
        manifest.with_name("UnrealEditor-UnrealMCP.lib").write_bytes(b"import library")

    def write_engine(self, folder: Path, major: int = 5, minor: int = 8) -> None:
        launcher = folder / "Engine" / "Build" / "BatchFiles" / "RunUAT.bat"
        launcher.parent.mkdir(parents=True)
        launcher.write_text("@echo off\r\n", encoding="utf-8")
        (folder / "Engine" / "Build" / "Build.version").write_text(
            json.dumps({"MajorVersion": major, "MinorVersion": minor}),
            encoding="utf-8",
        )

    def test_locate_project_requires_one_valid_descriptor(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            with self.assertRaises(deploy.DeploymentError):
                deploy.locate_project(folder)
            project = self.write_project(folder)
            self.assertEqual(project.descriptor.name, "Shooter.uproject")
            self.assertEqual(project.engine_association, "5.8")
            (folder / "Other.UPROJECT").write_text("{}", encoding="utf-8")
            with self.assertRaises(deploy.DeploymentError):
                deploy.locate_project(folder)

    def test_locate_project_rejects_invalid_engine_association(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            (folder / "Shooter.uproject").write_text(
                json.dumps({"FileVersion": 3, "EngineAssociation": 58}),
                encoding="utf-8",
            )
            with self.assertRaises(deploy.DeploymentError):
                deploy.locate_project(folder)

    def test_project_discovery_bounds_descriptor_and_directory(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            descriptor = folder / "Shooter.uproject"
            descriptor.write_bytes(b" " * (deploy.MAX_PROJECT_DESCRIPTOR_BYTES + 1))
            with self.assertRaisesRegex(deploy.DeploymentError, "larger than 1 MiB"):
                deploy.locate_project(folder)

            descriptor.write_text("{}", encoding="utf-8")
            (folder / "extra.txt").write_text("extra", encoding="utf-8")
            with mock.patch.object(deploy, "MAX_PROJECT_DIRECTORY_ENTRIES", 1):
                with self.assertRaisesRegex(deploy.DeploymentError, "more than 1 entries"):
                    deploy.locate_project(folder)

    def test_engine_candidates_prefer_exact_association_then_configuration(self):
        project = deploy.ProjectInfo(Path("D:/Game"), Path("D:/Game/Game.uproject"), "5.8")
        candidates = deploy.engine_candidates(
            project,
            environment={
                "UNREAL_MCP_ENGINE_ROOT": "D:/Configured/UE",
                "ProgramFiles": "C:/Program Files",
            },
            installations=[
                ("5.7", Path("D:/Epic/UE_5.7")),
                ("5.8", Path("D:/Epic/UE_5.8")),
                ("{custom}", Path("D:/Source/UE")),
            ],
        )
        self.assertEqual(
            candidates,
            [
                Path("D:/Epic/UE_5.8"),
                Path("C:/Program Files/Epic Games/UE_5.8"),
                Path("D:/Configured/UE"),
            ],
        )

    def test_default_engine_root_uses_trimmed_environment_value(self):
        self.assertEqual(
            deploy.default_engine_root(
                {"UNREAL_MCP_ENGINE_ROOT": "  C:/Program Files/Epic Games/UE_5.8  "}
            ),
            "C:/Program Files/Epic Games/UE_5.8",
        )
        self.assertEqual(deploy.default_engine_root({}), "")

    def test_build_command_is_fixed_to_installed_win64_package(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            engine = root / "UE_5.8"
            self.write_engine(engine)
            output = root / "Package"
            command = deploy.build_command(engine, output)
            self.assertIn("BuildPlugin", command)
            self.assertIn("-TargetPlatforms=Win64", command)
            self.assertIn("-Rocket", command)
            self.assertNotIn("-Unversioned", command)

    def test_engine_validation_rejects_unsupported_version(self):
        with tempfile.TemporaryDirectory() as temporary:
            engine = Path(temporary) / "UE_5.7"
            self.write_engine(engine, minor=7)
            with self.assertRaisesRegex(deploy.DeploymentError, "5.8 or newer"):
                deploy.validate_supported_engine_root(engine)

    def test_install_removes_source_and_debug_files_but_keeps_precompiled_metadata(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project_folder = root / "Game"
            project_folder.mkdir()
            project = self.write_project(project_folder)
            package = root / "Package"
            package.mkdir()
            self.write_package(package)

            destination = deploy.install_binary_plugin(
                package,
                project,
                replace_existing=False,
            )

            self.assertTrue((destination / "Binaries/Win64/UnrealEditor-UnrealMCP.dll").is_file())
            self.assertTrue(
                (
                    destination
                    / "Intermediate/Build/Win64/UnrealEditor/Development/UnrealMCP"
                    / "UnrealMCP.precompiled"
                ).is_file()
            )
            self.assertTrue(
                (destination / "Source/UnrealMCP/UnrealMCP.Build.cs").is_file()
            )
            self.assertIn(
                "bUsePrecompiled = true;",
                (destination / "Source/UnrealMCP/UnrealMCP.Build.cs").read_text(encoding="utf-8"),
            )
            self.assertEqual(list(destination.rglob("*.cpp")), [])
            self.assertEqual(list(destination.rglob("*.pdb")), [])
            deploy.verify_binary_plugin(destination)

    def test_replace_existing_plugin_does_not_mix_old_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project_folder = root / "Game"
            project_folder.mkdir()
            project = self.write_project(project_folder)
            old = project_folder / "Plugins" / "UnrealMCP"
            old.mkdir(parents=True)
            (old / "old.txt").write_text("old", encoding="utf-8")
            package = root / "Package"
            package.mkdir()
            self.write_package(package)

            destination = deploy.install_binary_plugin(package, project, replace_existing=True)

            self.assertFalse((destination / "old.txt").exists())
            self.assertEqual(list(destination.parent.glob(".UnrealMCP.backup-*")), [])

    def test_existing_plugin_requires_explicit_replacement(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project_folder = root / "Game"
            project_folder.mkdir()
            project = self.write_project(project_folder)
            (project_folder / "Plugins" / "UnrealMCP").mkdir(parents=True)
            package = root / "Package"
            package.mkdir()
            self.write_package(package)
            with self.assertRaises(deploy.DeploymentError):
                deploy.install_binary_plugin(package, project, replace_existing=False)

    def test_failed_post_install_verification_restores_existing_plugin(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project_folder = root / "Game"
            project_folder.mkdir()
            project = self.write_project(project_folder)
            old = project_folder / "Plugins" / "UnrealMCP"
            old.mkdir(parents=True)
            (old / "old.txt").write_text("old", encoding="utf-8")
            package = root / "Package"
            package.mkdir()
            self.write_package(package)
            real_verify = deploy.verify_binary_plugin
            calls = 0

            def fail_second_verification(plugin_root: Path) -> None:
                nonlocal calls
                calls += 1
                real_verify(plugin_root)
                if calls == 2:
                    raise deploy.DeploymentError("injected post-install failure")

            with mock.patch.object(
                deploy, "verify_binary_plugin", side_effect=fail_second_verification
            ):
                with self.assertRaisesRegex(deploy.DeploymentError, "injected"):
                    deploy.install_binary_plugin(package, project, replace_existing=True)
            self.assertEqual((old / "old.txt").read_text(encoding="utf-8"), "old")
            self.assertEqual(list(old.parent.glob(".UnrealMCP.backup-*")), [])

    def test_lm_studio_json_runs_checkout_server_for_exact_project(self):
        with tempfile.TemporaryDirectory() as temporary:
            project = self.write_project(Path(temporary))
            configuration = json.loads(
                deploy.lm_studio_json(project, Path("C:/Python312/python.exe"))
            )
            server = configuration["mcpServers"]["unreal-editor"]
            self.assertEqual(server["command"], str(Path("C:/Python312/python.exe").resolve()))
            self.assertEqual(server["args"], [str(deploy.SERVER_ENTRY), str(project.descriptor)])

    def test_verify_rejects_package_with_debug_artifacts(self):
        with tempfile.TemporaryDirectory() as temporary:
            package = Path(temporary)
            self.write_package(package)
            deploy.configure_precompiled_module_rules(package)
            with self.assertRaises(deploy.DeploymentError):
                deploy.verify_binary_plugin(package)

    def test_module_rule_configuration_requires_owned_insertion_point(self):
        with tempfile.TemporaryDirectory() as temporary:
            package = Path(temporary)
            self.write_package(package)
            rules = package / "Source/UnrealMCP/UnrealMCP.Build.cs"
            rules.write_text("// unexpected rules", encoding="utf-8")
            with self.assertRaises(deploy.DeploymentError):
                deploy.configure_precompiled_module_rules(package)

            rules.write_bytes(b" " * (deploy.MAX_MODULE_RULE_BYTES + 1))
            with self.assertRaisesRegex(deploy.DeploymentError, "larger than 64 KiB"):
                deploy.configure_precompiled_module_rules(package)

    def test_resolve_engine_reports_missing_association(self):
        project = deploy.ProjectInfo(Path("D:/Game"), Path("D:/Game/Game.uproject"), "{missing}")
        with mock.patch.object(deploy, "engine_candidates", return_value=[]):
            with self.assertRaisesRegex(deploy.DeploymentError, "select the engine folder manually"):
                deploy.resolve_engine_root(project)


if __name__ == "__main__":
    unittest.main()
