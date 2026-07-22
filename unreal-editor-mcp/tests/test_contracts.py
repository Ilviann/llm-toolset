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
        self.assertEqual(versions, {"0.13.0"})

    def test_only_released_phase_fourteen_commands_are_registered(self):
        names = [tool["name"] for tool in TOOLS]
        self.assertEqual(names, [
            "capabilities", "editor_state", "operation_status", "blueprint_inspect", "blueprint_action_catalog", "blueprint_graph_edit",
            "blueprint_create", "blueprint_compile", "blueprint_save",
            "blueprint_component_edit", "blueprint_default_edit",
            "blueprint_member_edit",
        ])
        bridge_source = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPBridge.cpp").read_text(encoding="utf-8")
        for command in names:
            self.assertIn(f'TEXT("{command}")', bridge_source)

    def test_phase_fourteen_family_policy_is_published_and_covered(self):
        policy = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPBlueprintFamilyPolicy.cpp").read_text(encoding="utf-8")
        bridge = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPBridge.cpp").read_text(encoding="utf-8")
        native_test = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/Tests/UnrealMCPAutomationTestsPhase14.cpp").read_text(encoding="utf-8")
        for family in ["actor", "game_mode_base", "game_mode", "game_state_base", "game_state"]:
            self.assertIn(f'TEXT("{family}")', policy)
        for family in ["game_mode_base", "game_mode", "game_state_base", "game_state"]:
            self.assertIn(f'TEXT("{family}")', native_test)
        self.assertIn('TEXT("parent_change"), false', policy)
        self.assertIn('TEXT("project_settings_assignment"), false', policy)
        self.assertIn('TEXT("blueprint_families")', bridge)

    def test_every_docs_directory_has_an_index(self):
        for directory in [path for path in (ROOT / "docs").rglob("*") if path.is_dir()]:
            with self.subTest(directory=directory):
                self.assertTrue((directory / "index.md").is_file())
