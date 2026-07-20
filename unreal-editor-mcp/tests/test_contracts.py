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
        self.assertEqual(versions, {"0.1.0"})

    def test_only_phase_one_commands_are_registered(self):
        names = [tool["name"] for tool in TOOLS]
        self.assertEqual(names, ["capabilities", "editor_state"])
        bridge_source = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPBridge.cpp").read_text(encoding="utf-8")
        allowed = re.search(r'Strings\(\{TEXT\("capabilities"\), TEXT\("editor_state"\)\}\)', bridge_source)
        self.assertIsNotNone(allowed)

    def test_every_docs_directory_has_an_index(self):
        for directory in [path for path in (ROOT / "docs").rglob("*") if path.is_dir()]:
            with self.subTest(directory=directory):
                self.assertTrue((directory / "index.md").is_file())
