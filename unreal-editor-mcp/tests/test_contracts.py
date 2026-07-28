import json
import re
import tomllib
import unittest
from pathlib import Path

import unreal_editor_mcp
from unreal_editor_mcp.tool_catalog import LARGE_TOOLS, TOOLS


ROOT = Path(__file__).resolve().parents[1]


class ReleaseContractTests(unittest.TestCase):
    def test_versions_match_executable_metadata(self):
        project = tomllib.loads((ROOT / "pyproject.toml").read_text(encoding="utf-8"))
        plugin = json.loads((ROOT / "plugin/UnrealMCP/UnrealMCP.uplugin").read_text(encoding="utf-8"))
        header = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Public/UnrealMCPVersion.h").read_text(encoding="utf-8")
        native = re.search(r'Version\[\].*TEXT\("([^"]+)"\)', header)
        self.assertIsNotNone(native)
        versions = {project["project"]["version"], plugin["VersionName"], native.group(1), unreal_editor_mcp.__version__}
        self.assertEqual(versions, {"0.20.0"})

    def test_only_released_asset_delete_commands_are_registered(self):
        names = [tool["name"] for tool in TOOLS]
        self.assertEqual(names, [
            "capabilities", "editor_state", "operation_status", "asset_references", "asset_delete",
            "level_inspect", "level_open",
            "blueprint_inspect", "blueprint_action_catalog", "blueprint_graph_edit",
            "blueprint_create", "blueprint_compile", "blueprint_save",
            "blueprint_component_edit", "blueprint_default_edit",
            "blueprint_member_edit", "gameplay_framework_edit", "game_data_inspect", "game_data_edit",
        ])
        bridge_source = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPBridge.cpp").read_text(encoding="utf-8")
        for command in names:
            self.assertIn(f'TEXT("{command}")', bridge_source)

    def test_asset_references_is_published_bounded_and_covered(self):
        bridge = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPBridge.cpp").read_text(encoding="utf-8")
        service = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPAssetReferenceService.cpp").read_text(encoding="utf-8")
        native_test = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/Tests/UnrealMCPAutomationTestsAssetReferences.cpp").read_text(encoding="utf-8")
        for feature in ["asset_reference_discovery", "asset_reference_live_memory"]:
            self.assertIn(f'TEXT("{feature}"), true', bridge)
        for bound in [
            "MaxAssetReferenceRegistryCandidates", "MaxAssetReferenceLiveObjects",
            "MaxAssetReferenceRecords", "MaxAssetReferenceRetainedCursors",
        ]:
            self.assertIn(bound, service)
        for evidence in ["serialized", "management", "searchable_name", "live_memory"]:
            self.assertIn(f'TEXT("{evidence}")', service)
        self.assertIn("UnrealMCP.AssetReferences.RegistryLiveMemoryAndCursors", native_test)

    def test_asset_delete_is_ledger_backed_conservative_and_covered(self):
        bridge = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPBridge.cpp").read_text(encoding="utf-8")
        service = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPAssetDeletionService.cpp").read_text(encoding="utf-8")
        native_test = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/Tests/UnrealMCPAutomationTestsAssetDelete.cpp").read_text(encoding="utf-8")
        self.assertIn('Command == TEXT("asset_delete")', bridge)
        self.assertIn('TEXT("asset_delete"), true', bridge)
        for safety in [
            "IsPlayingSessionInEditor()", "IsSimulatingInEditor()", "IsSavingPackage()",
            "IsGarbageCollecting()", "IsTransactionActive()", "IsAsyncLoading()",
            "GatherObjectReferencersForDeletion", "DeleteSingleObject", "CleanupAfterSuccessfulDelete",
        ]:
            self.assertIn(safety, service)
        self.assertNotIn("DeleteObjectsUnchecked", service)
        self.assertNotIn("ForceDeleteObjects", service)
        self.assertIn("UnrealMCP.AssetDelete.PreflightPersistenceAndReferences", native_test)

    def test_level_open_is_published_and_covered(self):
        bridge = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPBridge.cpp").read_text(encoding="utf-8")
        service = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPLevelService.cpp").read_text(encoding="utf-8")
        native_test = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/Tests/UnrealMCPAutomationTestsLevelOpen.cpp").read_text(encoding="utf-8")
        for feature in ["level_discovery", "level_open", "level_snapshots"]:
            self.assertIn(f'TEXT("{feature}"), true', bridge)
        for safety in ["IsPlayingSessionInEditor()", "IsSimulatingInEditor()", "IsSavingPackage()",
                       "IsGarbageCollecting()", "IsTransactionActive()", "IsAsyncLoading()"]:
            self.assertIn(safety, service)
        self.assertIn("FEditorFileUtils::LoadMap", service)
        self.assertIn("UnrealMCP.LevelOpen.DiscoverySnapshotsAndSafety", native_test)

    def test_phase_sixteen_multiplayer_policy_is_published_and_covered(self):
        policy = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPBlueprintFamilyPolicy.cpp").read_text(encoding="utf-8")
        bridge = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPBridge.cpp").read_text(encoding="utf-8")
        phase_fourteen_test = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/Tests/UnrealMCPAutomationTestsPhase14.cpp").read_text(encoding="utf-8")
        phase_fifteen_test = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/Tests/UnrealMCPAutomationTestsPhase15.cpp").read_text(encoding="utf-8")
        phase_sixteen_test = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/Tests/UnrealMCPAutomationTestsPhase16.cpp").read_text(encoding="utf-8")
        for family in ["actor", "game_mode_base", "game_mode", "game_state_base", "game_state", "game_instance"]:
            self.assertIn(f'TEXT("{family}")', policy)
        for family in ["game_mode_base", "game_mode", "game_state_base", "game_state"]:
            self.assertIn(f'TEXT("{family}")', phase_fourteen_test)
        self.assertIn('TEXT("game_instance")', phase_fifteen_test)
        self.assertIn('GetBoolField(TEXT("components"))', phase_fifteen_test)
        self.assertIn('TEXT("invalid_component")', phase_fifteen_test)
        self.assertIn('TEXT("parent_change"), false', policy)
        self.assertIn('TEXT("project_settings_assignment")', policy)
        self.assertIn('TEXT("server")', phase_sixteen_test)
        self.assertIn('TEXT("multicast")', phase_sixteen_test)
        self.assertIn('TEXT("gameplay_framework_edit")', bridge)
        self.assertIn('TEXT("blueprint_families")', bridge)
        self.assertIn('TEXT("game_instance_family"), true', bridge)

    def test_phase_seventeen_game_data_is_published_and_covered(self):
        bridge = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPBridge.cpp").read_text(encoding="utf-8")
        service = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPGameDataService.cpp").read_text(encoding="utf-8")
        test = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/Tests/UnrealMCPAutomationTestsPhase17.cpp").read_text(encoding="utf-8")
        for command in ["game_data_inspect", "game_data_edit"]:
            self.assertIn(f'TEXT("{command}")', bridge)
        for feature in ["user_defined_struct_authoring", "typed_data_tables", "game_data_batch_editing"]:
            self.assertIn(f'TEXT("{feature}"), true', bridge)
        for operation in ["add_member", "reorder_member", "add_row", "replace_row", "rename_row", "remove_row", "batch"]:
            self.assertIn(f'TEXT("{operation}")', service)
        self.assertIn('UnrealMCP.Phase17.GameDataAuthoring', test)

    def test_editor_lifecycle_is_large_mode_only_and_natively_guarded(self):
        self.assertNotIn("editor_lifecycle", [tool["name"] for tool in TOOLS])
        self.assertEqual([tool["name"] for tool in LARGE_TOOLS][-1], "editor_lifecycle")
        bridge = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPBridge.cpp").read_text(encoding="utf-8")
        protocol = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPProtocol.cpp").read_text(encoding="utf-8")
        native_test = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/Tests/UnrealMCPAutomationTestsLifecycle.cpp").read_text(encoding="utf-8")
        self.assertIn('TEXT("editor_shutdown")', bridge)
        self.assertIn('TEXT("editor_shutdown")', protocol)
        self.assertIn("IsTransactionActive()", bridge)
        self.assertIn("GetNumRemainingAssets()", bridge)
        self.assertIn("IsPlayingSessionInEditor()", bridge)
        self.assertIn("IsSavingPackage()", bridge)
        self.assertIn("RequestExit(false)", bridge)
        self.assertNotIn("RequestExit(true)", bridge)
        self.assertIn("rejects forced termination", native_test)

    def test_every_docs_directory_has_an_index(self):
        for directory in [path for path in (ROOT / "docs").rglob("*") if path.is_dir()]:
            with self.subTest(directory=directory):
                self.assertTrue((directory / "index.md").is_file())
