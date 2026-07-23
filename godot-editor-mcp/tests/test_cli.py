from __future__ import annotations

import os
import sys
import unittest
from unittest.mock import patch

from godot_editor_mcp import cli
from godot_editor_mcp.cli import _godot_executable, build_parser


class CommandLineTests(unittest.TestCase):
    def test_parser_accepts_godot_executable(self) -> None:
        args = build_parser().parse_args(
            ["project", "--godot-executable", "/custom/Godot"]
        )
        self.assertEqual(args.godot_executable, "/custom/Godot")

    def test_environment_configures_godot_executable(self) -> None:
        with patch.dict(
            os.environ, {"GODOT_EXECUTABLE": "/environment/Godot"}, clear=True
        ):
            self.assertEqual(_godot_executable(None), "/environment/Godot")

    def test_command_line_godot_executable_overrides_environment(self) -> None:
        with patch.dict(
            os.environ, {"GODOT_EXECUTABLE": "/environment/Godot"}, clear=True
        ):
            self.assertEqual(
                _godot_executable("/command-line/Godot"),
                "/command-line/Godot",
            )

    def test_missing_godot_executable_remains_unconfigured(self) -> None:
        with patch.dict(os.environ, {}, clear=True):
            self.assertIsNone(_godot_executable(None))

    def test_main_composes_launcher_with_command_line_override(self) -> None:
        arguments = [
            "godot-editor-mcp",
            "project",
            "--godot-executable",
            "/command-line/Godot",
        ]
        with (
            patch.object(sys, "argv", arguments),
            patch.dict(
                os.environ, {"GODOT_EXECUTABLE": "/environment/Godot"}, clear=True
            ),
            patch.object(cli, "GodotBridge") as bridge_type,
            patch.object(cli, "ProjectAssets") as assets_type,
            patch.object(cli, "EditorLauncher") as launcher_type,
            patch.object(cli, "run") as run,
        ):
            cli.main()

        launcher_type.assert_called_once_with("project", "/command-line/Godot")
        run.assert_called_once_with(
            bridge_type.return_value,
            assets_type.return_value,
            mode="tiny",
            launcher=launcher_type.return_value,
        )


if __name__ == "__main__":
    unittest.main()
