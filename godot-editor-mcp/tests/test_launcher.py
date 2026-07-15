from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from godot_editor_mcp.bridge import BridgeError
from godot_editor_mcp.launcher import EditorLauncher, LauncherError


class UnavailableBridge:
    def call(self, command: str) -> None:
        raise BridgeError("unavailable")


class AvailableBridge:
    def call(self, command: str) -> dict[str, bool]:
        return {"playing": False}


class EditorLauncherTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / "project.godot").write_text("[application]\n", encoding="utf-8")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_missing_configuration_is_rejected(self) -> None:
        launcher = EditorLauncher(self.root, None)
        with self.assertRaisesRegex(LauncherError, "set GODOT_EXECUTABLE"):
            launcher.start(UnavailableBridge())  # type: ignore[arg-type]

    def test_missing_project_raises_launcher_domain_error(self) -> None:
        with self.assertRaisesRegex(LauncherError, "project folder"):
            EditorLauncher(self.root / "missing", None)

    def test_relative_executable_is_rejected(self) -> None:
        launcher = EditorLauncher(self.root, "Godot")
        with self.assertRaisesRegex(LauncherError, "absolute path"):
            launcher.start(UnavailableBridge())  # type: ignore[arg-type]

    def _executable(self) -> Path:
        executable = self.root / "Godot"
        executable.write_bytes(b"binary")
        executable.chmod(0o755)
        return executable

    def test_posix_launch_uses_new_session_and_detached_stdio(self) -> None:
        executable = self._executable()
        launcher = EditorLauncher(self.root, str(executable))
        with (
            patch("godot_editor_mcp.launcher._is_windows", return_value=False),
            patch("godot_editor_mcp.launcher.subprocess.Popen") as popen,
        ):
            result = launcher.start(UnavailableBridge())  # type: ignore[arg-type]

        self.assertEqual(result, {"status": "started"})
        resolved_root = self.root.resolve()
        popen.assert_called_once_with(
            [str(executable.resolve()), "--editor", "--path", str(resolved_root)],
            stdin=-3,
            stdout=-3,
            stderr=-3,
            close_fds=True,
            start_new_session=True,
        )

    def test_windows_launch_uses_detached_process_group(self) -> None:
        executable = self._executable()
        launcher = EditorLauncher(self.root, str(executable))
        with (
            patch("godot_editor_mcp.launcher._is_windows", return_value=True),
            patch("godot_editor_mcp.launcher.subprocess.Popen") as popen,
        ):
            result = launcher.start(UnavailableBridge())  # type: ignore[arg-type]

        self.assertEqual(result, {"status": "started"})
        popen.assert_called_once_with(
            [str(executable.resolve()), "--editor", "--path", str(self.root.resolve())],
            stdin=-3,
            stdout=-3,
            stderr=-3,
            close_fds=True,
            creationflags=0x00000208,
        )

    def test_invalid_launch_options_are_reported_safely(self) -> None:
        executable = self._executable()
        launcher = EditorLauncher(self.root, str(executable))
        with patch(
            "godot_editor_mcp.launcher.subprocess.Popen",
            side_effect=ValueError("unsupported options"),
        ):
            with self.assertRaisesRegex(LauncherError, "could not be started"):
                launcher.start(UnavailableBridge())  # type: ignore[arg-type]

    def test_connected_editor_is_not_started_again(self) -> None:
        launcher = EditorLauncher(self.root, None)
        with patch("godot_editor_mcp.launcher.subprocess.Popen") as popen:
            result = launcher.start(AvailableBridge())  # type: ignore[arg-type]
        self.assertEqual(result, {"status": "already_running"})
        popen.assert_not_called()


if __name__ == "__main__":
    unittest.main()
