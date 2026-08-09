import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from scripts import deploy_plugin_windows as deploy
from scripts.windows_deployment import discovery, transaction, workflow


class WindowsDeploymentScriptTests(unittest.TestCase):
    def test_entrypoint_reexports_decomposed_owners(self):
        from scripts import windows_deployment
        from scripts.windows_deployment.models import DeploymentPlan, DeploymentRequest, DeploymentResult

        self.assertIs(deploy.main, windows_deployment.main)
        self.assertTrue(all(record is not None for record in (
            DeploymentRequest, DeploymentPlan, DeploymentResult,
        )))
        for module in (discovery, transaction, workflow):
            self.assertNotIn("tkinter", module.__dict__)

    def write_project(self, folder: Path, association: object = "5.8") -> deploy.ProjectInfo:
        descriptor = folder / "Shooter.uproject"
        descriptor.write_text(
            json.dumps({"FileVersion": 3, "EngineAssociation": association}),
            encoding="utf-8",
        )
        return deploy.locate_project(folder)

    def write_package(self, folder: Path, plugin_name: str = "UnrealMCP") -> None:
        (folder / f"{plugin_name}.uplugin").write_text(
            json.dumps({"Installed": True}),
            encoding="utf-8",
        )
        binary = folder / "Binaries" / "Win64" / f"UnrealEditor-{plugin_name}.dll"
        binary.parent.mkdir(parents=True)
        binary.write_bytes(b"binary")
        binary.with_suffix(".pdb").write_bytes(b"symbols")
        source = folder / "Source" / plugin_name / "Private" / "Module.cpp"
        source.parent.mkdir(parents=True)
        source.write_text("// source", encoding="utf-8")
        (folder / "Source" / plugin_name / f"{plugin_name}.Build.cs").write_text(
            f"public class {plugin_name}\n{{\n    public {plugin_name}()\n    {{\n"
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
            / plugin_name
            / f"{plugin_name}.precompiled"
        )
        manifest.parent.mkdir(parents=True)
        manifest.write_text("manifest", encoding="utf-8")
        manifest.with_name(f"UnrealEditor-{plugin_name}.lib").write_bytes(b"import library")

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
            with mock.patch.object(discovery, "MAX_PROJECT_DIRECTORY_ENTRIES", 1):
                with self.assertRaisesRegex(deploy.DeploymentError, "more than 1 entries"):
                    deploy.locate_project(folder)

    def test_engine_candidates_prefer_exact_association_then_configuration(self):
        project = deploy.ProjectInfo(Path("D:/Game"), Path("D:/Game/Game.uproject"), "5.8")
        candidates = deploy.engine_candidates(
            project,
            environment={
                "UE58": "D:/Configured/UE",
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
                {"UE58": "  C:/Program Files/Epic Games/UE_5.8  "}
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

    def test_gas_build_command_uses_companion_descriptor_and_base_dependency(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            engine = root / "UE_5.8"
            self.write_engine(engine)
            command = deploy.build_command(engine, root / "Package", deploy.GAS_PLUGIN)
            self.assertIn(f"-Plugin={deploy.package_plugin.GAS_DESCRIPTOR}", command)
            self.assertIn(f"-Dependencies={deploy.package_plugin.PLUGIN_DESCRIPTOR}", command)

    def test_commonui_build_command_uses_companion_descriptor_and_base_dependency(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            engine = root / "UE_5.8"
            self.write_engine(engine)
            command = deploy.build_command(engine, root / "Package", deploy.COMMONUI_PLUGIN)
            self.assertIn(f"-Plugin={deploy.package_plugin.COMMONUI_DESCRIPTOR}", command)
            self.assertIn(f"-Dependencies={deploy.package_plugin.PLUGIN_DESCRIPTOR}", command)

    def test_run_packaging_restores_gas_descriptor_contract_before_verification(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "Package"
            output.mkdir()
            (output / deploy.package_plugin.GAS_DESCRIPTOR.name).write_text(
                json.dumps({"Installed": True, "EngineVersion": "5.8.0"}),
                encoding="utf-8",
            )
            binary = output / "Binaries/Win64/UnrealEditor-UnrealMCPGAS.dll"
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b"binary")
            process = mock.Mock()
            process.stdout = iter(())
            process.wait.return_value = 0
            with (
                mock.patch.object(workflow, "build_command", return_value=["RunUAT.bat"]),
                mock.patch.object(workflow.subprocess, "Popen", return_value=process),
            ):
                workflow.run_packaging(
                    Path("C:/UE_5.8"), output, lambda message: None, deploy.GAS_PLUGIN
                )

            descriptor = json.loads(
                (output / deploy.package_plugin.GAS_DESCRIPTOR.name).read_text(
                    encoding="utf-8"
                )
            )
            self.assertEqual(descriptor["companion_api_version"], 1)
            self.assertEqual(
                descriptor["unreal_mcp_companion"]["owning_module"], "UnrealMCPGAS"
            )
            self.assertEqual(descriptor["Modules"][0]["LoadingPhase"], "None")
            self.assertFalse(descriptor["EnabledByDefault"])
            self.assertTrue(descriptor["Installed"])
            self.assertEqual(descriptor["EngineVersion"], "5.8.0")

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

    def test_install_can_keep_only_matching_win64_pdb_crash_symbols(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project_folder = root / "Game"
            project_folder.mkdir()
            project = self.write_project(project_folder)
            package = root / "Package"
            package.mkdir()
            self.write_package(package)
            binary_root = package / "Binaries" / "Win64"
            (binary_root / "Unrelated.pdb").write_bytes(b"unrelated symbols")
            (binary_root / "UnrealEditor-UnrealMCP.ipdb").write_bytes(b"incremental symbols")
            intermediate_pdb = (
                package
                / "Intermediate"
                / "Build"
                / "Win64"
                / "UnrealEditor"
                / "Development"
                / "UnrealMCP"
                / "UnrealMCP.pdb"
            )
            intermediate_pdb.write_bytes(b"intermediate symbols")

            destination = deploy.install_binary_plugin(
                package,
                project,
                replace_existing=False,
                include_pdb=True,
            )

            self.assertEqual(
                [path.name for path in destination.rglob("*.pdb")],
                ["UnrealEditor-UnrealMCP.pdb"],
            )
            self.assertEqual(list(destination.rglob("*.ipdb")), [])
            deploy.verify_binary_plugin(destination, include_pdb=True)

    def test_symbol_deployment_requires_a_pdb_matching_the_plugin_dll(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project_folder = root / "Game"
            project_folder.mkdir()
            project = self.write_project(project_folder)
            package = root / "Package"
            package.mkdir()
            self.write_package(package)
            (package / "Binaries" / "Win64" / "UnrealEditor-UnrealMCP.pdb").unlink()

            with self.assertRaisesRegex(deploy.DeploymentError, "missing matching Win64 PDB"):
                deploy.install_binary_plugin(
                    package,
                    project,
                    replace_existing=False,
                    include_pdb=True,
                )
            self.assertFalse((project_folder / "Plugins" / "UnrealMCP").exists())

    def test_deploy_forwards_the_pdb_checkbox_selection(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project_folder = root / "Game"
            project_folder.mkdir()
            project = self.write_project(project_folder)
            messages: list[str] = []

            def write_packaged_plugin(
                engine_root: Path,
                package_root: Path,
                log: object,
                plugin: deploy.PluginBuild,
            ) -> None:
                package_root.mkdir()
                self.write_package(package_root, plugin.name)

            with mock.patch.object(
                workflow,
                "run_packaging",
                side_effect=write_packaged_plugin,
            ):
                destinations = deploy.deploy(
                    project,
                    root / "UE_5.8",
                    replace_existing=False,
                    include_pdb=True,
                    log=messages.append,
                )

            self.assertTrue(
                (destinations[0] / "Binaries" / "Win64" / "UnrealEditor-UnrealMCP.pdb").is_file()
            )
            self.assertTrue(any("except matching Win64 PDBs" in message for message in messages))

    def test_project_install_destinations_and_descriptor_enable_both_plugins(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project_folder = root / "Game"
            project_folder.mkdir()
            project = self.write_project(project_folder)
            destinations = deploy.deployment_destinations(
                project,
                root / "UE_5.8",
                deploy.INSTALL_IN_PROJECT,
                include_gas=True,
            )
            self.assertEqual(
                destinations,
                (
                    project_folder / "Plugins" / "UnrealMCP",
                    project_folder / "Plugins" / "UnrealMCPGAS",
                ),
            )
            encoded = deploy.configured_project_descriptor(
                project,
                ("UnrealMCP", "UnrealMCPGAS"),
            )
            deploy.write_project_descriptor(project, encoded)
            references = {
                reference["Name"]: reference["Enabled"]
                for reference in json.loads(project.descriptor.read_text(encoding="utf-8"))["Plugins"]
            }
            self.assertEqual(references, {"UnrealMCP": True, "UnrealMCPGAS": True})

    def test_project_install_destinations_include_selected_commonui_companion(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project_folder = root / "Game"
            project_folder.mkdir()
            project = self.write_project(project_folder)
            destinations = deploy.deployment_destinations(
                project,
                root / "UE_5.8",
                deploy.INSTALL_IN_PROJECT,
                include_gas=False,
                include_commonui=True,
            )
            self.assertEqual(
                destinations,
                (
                    project_folder / "Plugins" / "UnrealMCP",
                    project_folder / "Plugins" / "UnrealMCPCommonUI",
                ),
            )

    def test_gas_and_commonui_companion_selections_are_independent_and_ordered(self):
        self.assertEqual(
            deploy.selected_plugins(include_gas=False, include_commonui=False),
            (deploy.BASE_PLUGIN,),
        )
        self.assertEqual(
            deploy.selected_plugins(include_gas=True, include_commonui=True),
            (deploy.BASE_PLUGIN, deploy.GAS_PLUGIN, deploy.COMMONUI_PLUGIN),
        )

    def test_project_descriptor_enable_rejects_duplicate_owned_reference(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            descriptor = folder / "Shooter.uproject"
            descriptor.write_text(
                json.dumps(
                    {
                        "EngineAssociation": "5.8",
                        "Plugins": [
                            {"Name": "UnrealMCP", "Enabled": False},
                            {"Name": "unrealmcp", "Enabled": True},
                        ],
                    }
                ),
                encoding="utf-8",
            )
            project = deploy.locate_project(folder)
            with self.assertRaisesRegex(deploy.DeploymentError, "duplicate UnrealMCP"):
                deploy.configured_project_descriptor(project, ("UnrealMCP",))

    def test_engine_install_sets_requested_default_for_all_selected_plugins(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            engine = root / "UE_5.8"
            self.write_engine(engine)
            plugins = (deploy.BASE_PLUGIN, deploy.GAS_PLUGIN, deploy.COMMONUI_PLUGIN)
            packages = []
            destinations = []
            for plugin in plugins:
                package = root / f"{plugin.name}Package"
                package.mkdir()
                self.write_package(package, plugin.name)
                destination = deploy.engine_plugin_destination(engine, plugin.name)
                packages.append((plugin, package, destination))
                destinations.append(destination)

            installed = deploy.install_binary_plugins(
                packages,
                replace_existing=False,
                enabled_by_default=True,
            )

            self.assertEqual(installed, tuple(destinations))
            for plugin, destination in zip(plugins, installed):
                descriptor = json.loads(
                    (destination / f"{plugin.name}.uplugin").read_text(encoding="utf-8")
                )
                self.assertIs(descriptor["EnabledByDefault"], True)

    def test_three_plugin_install_rolls_back_when_project_enable_fails(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            destination_root = root / "Plugins"
            packages = []
            plugins = (deploy.BASE_PLUGIN, deploy.GAS_PLUGIN, deploy.COMMONUI_PLUGIN)
            for plugin in plugins:
                package = root / f"{plugin.name}Package"
                package.mkdir()
                self.write_package(package, plugin.name)
                destination = destination_root / plugin.name
                destination.mkdir(parents=True)
                (destination / "old.txt").write_text(plugin.name, encoding="utf-8")
                packages.append((plugin, package, destination))

            with self.assertRaisesRegex(deploy.DeploymentError, "enable failed"):
                deploy.install_binary_plugins(
                    packages,
                    replace_existing=True,
                    after_install=lambda: (_ for _ in ()).throw(
                        deploy.DeploymentError("enable failed")
                    ),
                )

            for plugin in plugins:
                self.assertEqual(
                    (destination_root / plugin.name / "old.txt").read_text(encoding="utf-8"),
                    plugin.name,
                )
            self.assertEqual(list(destination_root.glob(".*.backup-*")), [])

    def test_deploy_builds_and_enables_gas_with_base_for_project_install(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project_folder = root / "Game"
            project_folder.mkdir()
            project = self.write_project(project_folder)
            built: list[str] = []

            def write_packaged_plugin(
                engine_root: Path,
                package_root: Path,
                log: object,
                plugin: deploy.PluginBuild,
            ) -> None:
                built.append(plugin.name)
                package_root.mkdir()
                self.write_package(package_root, plugin.name)

            with mock.patch.object(workflow, "run_packaging", side_effect=write_packaged_plugin):
                installed = deploy.deploy(
                    project,
                    root / "UE_5.8",
                    replace_existing=False,
                    include_gas=True,
                    install_method=deploy.INSTALL_IN_PROJECT,
                    log=lambda message: None,
                )

            self.assertEqual(built, ["UnrealMCP", "UnrealMCPGAS"])
            self.assertEqual([path.name for path in installed], ["UnrealMCP", "UnrealMCPGAS"])
            references = json.loads(project.descriptor.read_text(encoding="utf-8"))["Plugins"]
            self.assertEqual(
                references,
                [
                    {"Name": "UnrealMCP", "Enabled": True},
                    {"Name": "UnrealMCPGAS", "Enabled": True},
                ],
            )

    def test_deploy_builds_and_enables_commonui_with_base_for_project_install(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project_folder = root / "Game"
            project_folder.mkdir()
            project = self.write_project(project_folder)
            built: list[str] = []

            def write_packaged_plugin(
                engine_root: Path,
                package_root: Path,
                log: object,
                plugin: deploy.PluginBuild,
            ) -> None:
                built.append(plugin.name)
                package_root.mkdir()
                self.write_package(package_root, plugin.name)

            with mock.patch.object(workflow, "run_packaging", side_effect=write_packaged_plugin):
                installed = deploy.deploy(
                    project,
                    root / "UE_5.8",
                    replace_existing=False,
                    include_commonui=True,
                    install_method=deploy.INSTALL_IN_PROJECT,
                    log=lambda message: None,
                )

            self.assertEqual(built, ["UnrealMCP", "UnrealMCPCommonUI"])
            self.assertEqual([path.name for path in installed], ["UnrealMCP", "UnrealMCPCommonUI"])
            references = json.loads(project.descriptor.read_text(encoding="utf-8"))["Plugins"]
            self.assertEqual(
                references,
                [
                    {"Name": "UnrealMCP", "Enabled": True},
                    {"Name": "UnrealMCPCommonUI", "Enabled": True},
                ],
            )

    def test_engine_install_without_default_enablement_sets_false_and_leaves_project_unchanged(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project_folder = root / "Game"
            project_folder.mkdir()
            project = self.write_project(project_folder)
            original_project = project.descriptor.read_bytes()
            engine = root / "UE_5.8"
            self.write_engine(engine)

            def write_packaged_plugin(
                engine_root: Path,
                package_root: Path,
                log: object,
                plugin: deploy.PluginBuild,
            ) -> None:
                package_root.mkdir()
                self.write_package(package_root, plugin.name)

            with mock.patch.object(workflow, "run_packaging", side_effect=write_packaged_plugin):
                installed = deploy.deploy(
                    project,
                    engine,
                    replace_existing=False,
                    include_gas=True,
                    install_method=deploy.INSTALL_IN_ENGINE_DISABLED,
                    log=lambda message: None,
                )

            self.assertEqual(project.descriptor.read_bytes(), original_project)
            self.assertTrue(all("Marketplace" in destination.parts for destination in installed))
            for plugin_name, destination in zip(("UnrealMCP", "UnrealMCPGAS"), installed):
                descriptor = json.loads(
                    (destination / f"{plugin_name}.uplugin").read_text(encoding="utf-8")
                )
                self.assertIs(descriptor["EnabledByDefault"], False)

    def test_install_method_and_companion_flag_are_exact(self):
        project = deploy.ProjectInfo(Path("D:/Game"), Path("D:/Game/Game.uproject"), "5.8")
        with self.assertRaisesRegex(deploy.DeploymentError, "unsupported install method"):
            deploy.deployment_destinations(
                project,
                Path("D:/UE_5.8"),
                "engine",
                include_gas=False,
            )
        with self.assertRaisesRegex(deploy.DeploymentError, "include_gas must be Boolean"):
            deploy.deployment_destinations(
                project,
                Path("D:/UE_5.8"),
                deploy.INSTALL_IN_PROJECT,
                include_gas=1,  # type: ignore[arg-type]
            )
        with self.assertRaisesRegex(deploy.DeploymentError, "include_commonui must be Boolean"):
            deploy.deployment_destinations(
                project,
                Path("D:/UE_5.8"),
                deploy.INSTALL_IN_PROJECT,
                include_gas=False,
                include_commonui=1,  # type: ignore[arg-type]
            )

    def test_project_install_rolls_back_if_descriptor_changes_during_build(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project_folder = root / "Game"
            project_folder.mkdir()
            project = self.write_project(project_folder)

            def write_package_and_change_project(
                engine_root: Path,
                package_root: Path,
                log: object,
                plugin: deploy.PluginBuild,
            ) -> None:
                package_root.mkdir()
                self.write_package(package_root, plugin.name)
                project.descriptor.write_text(
                    json.dumps({"EngineAssociation": "5.8", "ExternalChange": True}),
                    encoding="utf-8",
                )

            with mock.patch.object(
                workflow,
                "run_packaging",
                side_effect=write_package_and_change_project,
            ):
                with self.assertRaisesRegex(deploy.DeploymentError, "changed while plugins were building"):
                    deploy.deploy(
                        project,
                        root / "UE_5.8",
                        replace_existing=False,
                        install_method=deploy.INSTALL_IN_PROJECT,
                        log=lambda message: None,
                    )

            self.assertFalse((project_folder / "Plugins" / "UnrealMCP").exists())
            self.assertIs(
                json.loads(project.descriptor.read_text(encoding="utf-8"))["ExternalChange"],
                True,
            )

    def test_deploy_rejects_plugin_destination_state_change_during_build(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project_folder = root / "Game"
            project_folder.mkdir()
            project = self.write_project(project_folder)
            destination = project_folder / "Plugins" / "UnrealMCP"

            def write_package_and_create_destination(
                engine_root: Path,
                package_root: Path,
                log: object,
                plugin: deploy.PluginBuild,
            ) -> None:
                package_root.mkdir()
                self.write_package(package_root, plugin.name)
                destination.mkdir(parents=True)
                (destination / "external.txt").write_text("external", encoding="utf-8")

            with mock.patch.object(
                workflow,
                "run_packaging",
                side_effect=write_package_and_create_destination,
            ):
                with self.assertRaisesRegex(deploy.DeploymentError, "state changed"):
                    deploy.deploy(
                        project,
                        root / "UE_5.8",
                        replace_existing=False,
                        install_method=deploy.INSTALL_IN_PROJECT,
                        log=lambda message: None,
                    )

            self.assertEqual((destination / "external.txt").read_text(encoding="utf-8"), "external")

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

            def fail_second_verification(
                plugin_root: Path,
                *,
                include_pdb: bool = False,
                plugin_name: str = "UnrealMCP",
                enabled_by_default: bool | None = None,
            ) -> None:
                nonlocal calls
                calls += 1
                real_verify(
                    plugin_root,
                    include_pdb=include_pdb,
                    plugin_name=plugin_name,
                    enabled_by_default=enabled_by_default,
                )
                if calls == 2:
                    raise deploy.DeploymentError("injected post-install failure")

            with mock.patch.object(
                transaction, "verify_binary_plugin", side_effect=fail_second_verification
            ):
                with self.assertRaisesRegex(deploy.DeploymentError, "injected"):
                    deploy.install_binary_plugin(package, project, replace_existing=True)
            self.assertEqual((old / "old.txt").read_text(encoding="utf-8"), "old")
            self.assertEqual(list(old.parent.glob(".UnrealMCP.backup-*")), [])

    def test_lm_studio_json_runs_checkout_server_for_exact_project(self):
        with tempfile.TemporaryDirectory() as temporary:
            project = self.write_project(Path(temporary))
            definition = deploy.mcp_server_definition(
                project, Path("C:/Python312/python.exe")
            )
            configuration = json.loads(
                deploy.lm_studio_json(project, Path("C:/Python312/python.exe"))
            )
            server = configuration["mcpServers"]["unreal-editor"]
            self.assertEqual(server, definition)
            self.assertEqual(server["command"], str(Path("C:/Python312/python.exe").resolve()))
            self.assertEqual(server["args"], [str(deploy.SERVER_ENTRY), str(project.descriptor)])

    def test_lm_studio_json_exposes_independent_access_and_lifecycle_options(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project_folder = root / "Project"
            project_folder.mkdir()
            project = self.write_project(project_folder)
            editor = root / "Engine" / deploy.WINDOWS_EDITOR_RELATIVE
            editor.parent.mkdir(parents=True)
            editor.write_bytes(b"editor")

            writable = json.loads(deploy.lm_studio_json(project, writable=True))
            self.assertEqual(
                writable["mcpServers"]["unreal-editor"]["args"],
                [str(deploy.SERVER_ENTRY), str(project.descriptor), "--writable"],
            )
            lifecycle = json.loads(deploy.lm_studio_json(project, editor_lifecycle=editor))
            self.assertEqual(
                lifecycle["mcpServers"]["unreal-editor"]["args"],
                [
                    str(deploy.SERVER_ENTRY),
                    str(project.descriptor),
                    "--editor-lifecycle",
                    str(editor.resolve()),
                ],
            )
            both = json.loads(
                deploy.lm_studio_json(project, writable=True, editor_lifecycle=editor)
            )
            definition = deploy.mcp_server_definition(
                project, writable=True, editor_lifecycle=editor
            )
            self.assertEqual(both["mcpServers"][deploy.SERVER_NAME], definition)
            self.assertEqual(
                both["mcpServers"]["unreal-editor"]["args"],
                [
                    str(deploy.SERVER_ENTRY),
                    str(project.descriptor),
                    "--writable",
                    "--editor-lifecycle",
                    str(editor.resolve()),
                ],
            )

    def test_lifecycle_executable_validation_is_fail_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            engine = root / "UE_5.8"
            editor = engine / deploy.WINDOWS_EDITOR_RELATIVE
            editor.parent.mkdir(parents=True)
            editor.write_bytes(b"editor")
            self.assertEqual(
                deploy.windows_editor_lifecycle_executable(engine),
                editor.resolve(),
            )
            with self.assertRaisesRegex(deploy.DeploymentError, "absolute path"):
                deploy.validate_editor_lifecycle_executable(Path("UnrealEditor.exe"))
            with self.assertRaisesRegex(deploy.DeploymentError, "existing regular file"):
                deploy.validate_editor_lifecycle_executable(root / "Missing" / "UnrealEditor.exe")
            wrong = root / "Editor.exe"
            wrong.write_bytes(b"editor")
            with self.assertRaisesRegex(deploy.DeploymentError, "must be UnrealEditor.exe"):
                deploy.validate_editor_lifecycle_executable(wrong)
            with self.assertRaisesRegex(deploy.DeploymentError, "writable must be Boolean"):
                project_folder = root / "Project"
                project_folder.mkdir()
                deploy.lm_studio_json(self.write_project(project_folder), writable=1)  # type: ignore[arg-type]

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
        with mock.patch.object(discovery, "engine_candidates", return_value=[]):
            with self.assertRaisesRegex(deploy.DeploymentError, "select the engine folder manually"):
                deploy.resolve_engine_root(project)


if __name__ == "__main__":
    unittest.main()
