"""Validated Unreal Engine installation boundary for support tools."""

from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping

from .errors import ToolingError
from .paths import read_json_object, resolved


ENGINE_ROOT_ENV = "UE58"
MIN_UNREAL_ENGINE_VERSION = (5, 8)
MAX_ENGINE_VERSION_BYTES = 64 * 1024


@dataclass(frozen=True)
class EngineInstallation:
    root: Path
    version: tuple[int, int]
    run_uat: Path
    editor_command: Path
    editor_gui: Path | None


def read_engine_version(engine_root: Path) -> tuple[int, int]:
    root = resolved(engine_root)
    version_file = root / "Engine" / "Build" / "Build.version"
    try:
        value = read_json_object(
            version_file,
            label="Unreal Engine build version",
            maximum_bytes=MAX_ENGINE_VERSION_BYTES,
        )
    except ToolingError as error:
        message = str(error).replace(
            f"larger than {MAX_ENGINE_VERSION_BYTES} bytes",
            "larger than 64 KiB",
        )
        raise ToolingError(message) from error
    major = value.get("MajorVersion")
    minor = value.get("MinorVersion")
    if type(major) is not int or type(minor) is not int:
        raise ToolingError(
            f"Unreal Engine build version has invalid major/minor fields: {version_file}"
        )
    if (major, minor) < MIN_UNREAL_ENGINE_VERSION:
        minimum = ".".join(str(part) for part in MIN_UNREAL_ENGINE_VERSION)
        raise ToolingError(
            f"Unreal MCP requires Unreal Engine {minimum} or newer; "
            f"selected Engine is {major}.{minor}"
        )
    return major, minor


def validate_engine_installation(engine_root: Path, host_system: str) -> EngineInstallation:
    root = resolved(engine_root)
    if not (root / "Engine").is_dir():
        raise ToolingError(f"engine root must contain an Engine directory: {root}")
    host_paths = {
        "Windows": (
            Path("Engine/Build/BatchFiles/RunUAT.bat"),
            Path("Engine/Binaries/Win64/UnrealEditor-Cmd.exe"),
            Path("Engine/Binaries/Win64/UnrealEditor.exe"),
        ),
        "Darwin": (
            Path("Engine/Build/BatchFiles/RunUAT.sh"),
            Path("Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"),
            None,
        ),
        "Linux": (
            Path("Engine/Build/BatchFiles/RunUAT.sh"),
            Path("Engine/Binaries/Linux/UnrealEditor"),
            None,
        ),
    }
    relative = host_paths.get(host_system)
    if relative is None:
        raise ToolingError(f"unsupported host platform: {host_system}")
    uat_relative, editor_relative, gui_relative = relative
    run_uat = root / uat_relative
    if not run_uat.is_file():
        raise ToolingError(f"Unreal AutomationTool launcher does not exist: {run_uat}")
    if host_system != "Windows" and not os.access(run_uat, os.X_OK):
        raise ToolingError(f"Unreal AutomationTool launcher is not executable: {run_uat}")
    version = read_engine_version(root)
    return EngineInstallation(
        root=root,
        version=version,
        run_uat=run_uat,
        editor_command=root / editor_relative,
        editor_gui=root / gui_relative if gui_relative is not None else None,
    )


def configure_build_environment(
    host_system: str,
    developer_dir: Path | None,
    environment: Mapping[str, str] | None = None,
) -> dict[str, str]:
    source = os.environ if environment is None else environment
    configured_environment = dict(source)
    if host_system != "Darwin":
        return configured_environment
    configured = developer_dir
    if configured is None:
        value = source.get("UNREAL_MCP_DEVELOPER_DIR") or source.get("DEVELOPER_DIR")
        configured = Path(value) if value else None
    if configured is None:
        raise ToolingError(
            "UNREAL_MCP_DEVELOPER_DIR or --developer-dir is required for a reproducible macOS build"
        )
    configured = resolved(configured)
    if not (configured / "usr" / "bin" / "xcodebuild").is_file():
        raise ToolingError(
            f"developer directory does not contain usr/bin/xcodebuild: {configured}"
        )
    configured_environment["DEVELOPER_DIR"] = str(configured)
    return configured_environment
