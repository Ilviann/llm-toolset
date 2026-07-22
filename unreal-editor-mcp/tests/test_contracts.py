import json
import re
import tomllib
import unittest
from pathlib import Path

import unreal_editor_mcp
from unreal_editor_mcp.tool_catalog import TOOLS


ROOT = Path(__file__).resolve().parents[1]


class ReleaseContractTests(unittest.TestCase):
    def test_versions_match_executable_metadata(self):
        project = tomllib.loads((ROOT / "pyproject.toml").read_text(encoding="utf-8"))
        plugin = json.loads((ROOT / "plugin/UnrealMCP/UnrealMCP.uplugin").read_text(encoding="utf-8"))
        header = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Public/UnrealMCPVersion.h").read_text(encoding="utf-8")
        native = re.search(r'Version\[\].*TEXT\("([^"]+)"\)', header)
        self.assertIsNotNone(native)
        versions = {project["project"]["version"], plugin["VersionName"], native.group(1), unreal_editor_mcp.__version__}
        self.assertEqual(versions, {"0.15.0"})

    def test_only_released_phase_sixteen_commands_are_registered(self):
        names = [tool["name"] for tool in TOOLS]
        self.assertEqual(names, [
            "capabilities", "editor_state", "operation_status", "blueprint_inspect", "blueprint_action_catalog", "blueprint_graph_edit",
            "blueprint_create", "blueprint_compile", "blueprint_save",
            "blueprint_component_edit", "blueprint_default_edit",
            "blueprint_member_edit", "gameplay_framework_edit",
        ])
        bridge_source = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPBridge.cpp").read_text(encoding="utf-8")
        for command in names:
            self.assertIn(f'TEXT("{command}")', bridge_source)

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

    def test_every_docs_directory_has_an_index(self):
        for directory in [path for path in (ROOT / "docs").rglob("*") if path.is_dir()]:
            with self.subTest(directory=directory):
                self.assertTrue((directory / "index.md").is_file())
