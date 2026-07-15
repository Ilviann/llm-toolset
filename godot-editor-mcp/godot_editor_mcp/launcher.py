"""Narrow, opt-in launcher for the configured Godot editor."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
from typing import Any

from .bridge import BridgeError, GodotBridge
from .errors import LauncherError


def _is_windows() -> bool:
    return os.name == "nt"


class EditorLauncher:
    def __init__(self, project: str | Path, executable: str | None) -> None:
        try:
            self.project = Path(project).expanduser().resolve(strict=True)
        except (OSError, RuntimeError):
            raise LauncherError("Godot project folder does not exist") from None
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
        popen_options: dict[str, Any] = {
            "stdin": subprocess.DEVNULL,
            "stdout": subprocess.DEVNULL,
            "stderr": subprocess.DEVNULL,
            "close_fds": True,
        }
        if _is_windows():
            # These stable Win32 values are defined by subprocess on Windows.
            # Numeric fallbacks keep this module testable on POSIX hosts.
            detached_process = getattr(subprocess, "DETACHED_PROCESS", 0x00000008)
            new_process_group = getattr(
                subprocess, "CREATE_NEW_PROCESS_GROUP", 0x00000200
            )
            popen_options["creationflags"] = detached_process | new_process_group
        else:
            popen_options["start_new_session"] = True
        try:
            self._process = subprocess.Popen(
                [str(executable), "--editor", "--path", str(self.project)],
                **popen_options,
            )
        except (OSError, ValueError) as exc:
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
