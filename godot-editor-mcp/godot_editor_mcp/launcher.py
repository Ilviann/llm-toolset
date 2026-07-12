"""Narrow, opt-in launcher for the configured Godot editor."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess

from .bridge import BridgeError, GodotBridge


class LauncherError(Exception):
    """A concise launcher error safe to return to an MCP client."""


class EditorLauncher:
    def __init__(self, project: str | Path, executable: str | None) -> None:
        self.project = Path(project).expanduser().resolve(strict=True)
        self.executable = executable.strip() if executable else None
        self._process: subprocess.Popen[bytes] | None = None

    @property
    def configured(self) -> bool:
        return self.executable is not None

    def start(self, bridge: GodotBridge) -> dict[str, str]:
        try:
            bridge.call("state")
        except BridgeError:
            pass
        else:
            return {"status": "already_running"}

        if self._process is not None and self._process.poll() is None:
            return {"status": "starting"}

        executable = self._validated_executable()
        try:
            self._process = subprocess.Popen(
                [str(executable), "--editor", "--path", str(self.project)],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                close_fds=True,
                start_new_session=True,
            )
        except OSError as exc:
            raise LauncherError(f"Godot editor could not be started: {exc}") from None
        return {"status": "started"}

    def _validated_executable(self) -> Path:
        if self.executable is None:
            raise LauncherError(
                "Godot executable is not configured; set GODOT_EXECUTABLE"
            )
        candidate = Path(self.executable).expanduser()
        if not candidate.is_absolute():
            raise LauncherError("GODOT_EXECUTABLE must be an absolute path")
        try:
            executable = candidate.resolve(strict=True)
        except OSError:
            raise LauncherError("GODOT_EXECUTABLE does not exist") from None
        if not executable.is_file() or not os.access(executable, os.X_OK):
            raise LauncherError("GODOT_EXECUTABLE must be an executable file")
        return executable
