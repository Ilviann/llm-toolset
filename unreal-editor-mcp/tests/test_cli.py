import io
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

from unreal_editor_mcp.cli import build_parser, main


class CliTests(unittest.TestCase):
    def test_default_and_new_flags_parse_independently(self):
        parser = build_parser()
        cases = (
            (["Project.uproject"], False, None),
            (["Project.uproject", "--writable"], True, None),
            (["Project.uproject", "--editor-lifecycle", "C:\\UE\\UnrealEditor.exe"],
             False, "C:\\UE\\UnrealEditor.exe"),
            (["Project.uproject", "--writable", "--editor-lifecycle",
              "C:\\UE\\UnrealEditor.exe"], True, "C:\\UE\\UnrealEditor.exe"),
        )
        for arguments, writable, lifecycle in cases:
            with self.subTest(arguments=arguments):
                parsed = parser.parse_args(arguments)
                self.assertEqual(parsed.writable, writable)
                self.assertEqual(parsed.editor_lifecycle, lifecycle)

    def test_removed_options_are_rejected(self):
        parser = build_parser()
        for arguments in (
            ["Project.uproject", "--tool-mode", "large"],
            ["Project.uproject", "--editor", "C:\\UE\\UnrealEditor.exe"],
        ):
                with (
                    self.subTest(arguments=arguments),
                    patch("sys.stderr", new=io.StringIO()),
                    self.assertRaises(SystemExit),
                ):
                    parser.parse_args(arguments)

    def test_main_composes_all_access_and_lifecycle_combinations(self):
        with tempfile.TemporaryDirectory() as temporary:
            descriptor = Path(temporary) / "Example.uproject"
            descriptor.write_text("{}", encoding="utf-8")
            executable = Path(temporary) / "UnrealEditor.exe"
            executable.write_bytes(b"MZ")
            cases = (
                ([], False, False),
                (["--writable"], True, False),
                (["--editor-lifecycle", str(executable)], False, True),
                (["--writable", "--editor-lifecycle", str(executable)], True, True),
            )
            for options, writable, lifecycle_enabled in cases:
                with self.subTest(options=options):
                    bridge = Mock()
                    lifecycle = Mock()
                    captured = []
                    argv = ["unreal-editor-mcp", str(descriptor), *options]
                    with (
                        patch("sys.argv", argv),
                        patch("unreal_editor_mcp.cli.UnrealBridge", return_value=bridge),
                        patch("unreal_editor_mcp.cli.resolve_editor_executable",
                              return_value=executable.resolve()) as resolve,
                        patch("unreal_editor_mcp.cli.EditorLifecycle",
                              return_value=lifecycle) as lifecycle_type,
                        patch("unreal_editor_mcp.cli.serve", side_effect=captured.append),
                    ):
                        main()
                    self.assertEqual(len(captured), 1)
                    server = captured[0]
                    self.assertEqual(server.writable, writable)
                    self.assertIs(server.lifecycle, lifecycle if lifecycle_enabled else None)
                    if lifecycle_enabled:
                        resolve.assert_called_once()
                        lifecycle_type.assert_called_once()
                    else:
                        resolve.assert_not_called()
                        lifecycle_type.assert_not_called()

    def test_main_rejects_relative_or_missing_lifecycle_executable(self):
        with tempfile.TemporaryDirectory() as temporary:
            descriptor = Path(temporary) / "Example.uproject"
            descriptor.write_text("{}", encoding="utf-8")
            for value in ("UnrealEditor.exe", str(Path(temporary) / "missing" / "UnrealEditor.exe")):
                argv = ["unreal-editor-mcp", str(descriptor), "--editor-lifecycle", value]
                with (
                    self.subTest(value=value),
                    patch("sys.argv", argv),
                    patch("sys.stderr", new=io.StringIO()),
                    self.assertRaises(SystemExit),
                ):
                    main()


if __name__ == "__main__":
    unittest.main()
