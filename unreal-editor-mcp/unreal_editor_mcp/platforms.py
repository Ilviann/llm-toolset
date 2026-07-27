"""Injectable platform behavior for path identity and process discovery."""

from __future__ import annotations

import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path, PurePosixPath, PureWindowsPath
from typing import Any, Callable

from .errors import ConfigurationError


def current_system() -> str:
    if sys.platform == "win32":
        return "windows"
    if sys.platform == "darwin":
        return "macos"
    return "linux"


@dataclass(frozen=True)
class PlatformAdapter:
    system: str
    process_probe: Callable[[int], bool] | None = None

    def __post_init__(self) -> None:
        if self.system not in {"macos", "windows", "linux"}:
            raise ValueError("Unsupported platform")

    def path_identity(self, value: str) -> str:
        if self.system == "windows":
            return PureWindowsPath(value).as_posix().casefold()
        return PurePosixPath(value).as_posix()

    def process_is_alive(self, process_id: int) -> bool:
        if self.process_probe is not None:
            return bool(self.process_probe(process_id))
        if self.system == "windows":
            return _windows_process_is_alive(process_id)
        try:
            os.kill(process_id, 0)
        except ProcessLookupError:
            return False
        except PermissionError:
            return True
        return True

    def editor_launch_command(self, executable: Path, project: Path) -> tuple[str, str]:
        if self.system == "linux":
            raise ConfigurationError("Configured editor launch is supported only on macOS and Windows")
        return (str(executable), str(project))

    def launch_editor(
        self,
        executable: Path,
        project: Path,
        *,
        process_factory: Callable[..., Any] = subprocess.Popen,
    ) -> Any:
        command = self.editor_launch_command(executable, project)
        options: dict[str, Any] = {
            "cwd": str(project.parent),
            "stdin": subprocess.DEVNULL,
            "stdout": subprocess.DEVNULL,
            "stderr": subprocess.DEVNULL,
            "close_fds": True,
            "shell": False,
        }
        if self.system == "windows":
            # Stable Win32 DETACHED_PROCESS and CREATE_NEW_PROCESS_GROUP values.
            options["creationflags"] = 0x00000008 | 0x00000200
        else:
            options["start_new_session"] = True
        return process_factory(command, **options)


def _windows_process_is_alive(process_id: int) -> bool:
    if sys.platform != "win32":
        return False
    import ctypes

    process_query_limited_information = 0x1000
    handle = ctypes.windll.kernel32.OpenProcess(process_query_limited_information, False, process_id)
    if not handle:
        return False
    ctypes.windll.kernel32.CloseHandle(handle)
    return True


DEFAULT_PLATFORM = PlatformAdapter(current_system())
