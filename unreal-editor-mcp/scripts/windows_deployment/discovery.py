"""Bounded Unreal project and Windows Engine discovery."""

from __future__ import annotations

import os
import platform
from pathlib import Path
from typing import Mapping, Sequence

try:
    from scripts.packaging import ENGINE_ROOT_ENV, PackagingError, validate_engine_root
    from scripts.unreal_tooling.paths import read_json_object as read_bounded_json
    from scripts.unreal_tooling.paths import resolved
except ModuleNotFoundError:
    from packaging import ENGINE_ROOT_ENV, PackagingError, validate_engine_root  # type: ignore[no-redef]
    from unreal_tooling.paths import read_json_object as read_bounded_json  # type: ignore[no-redef]
    from unreal_tooling.paths import resolved  # type: ignore[no-redef]

from .models import ProjectInfo


MAX_PROJECT_DESCRIPTOR_BYTES = 1024 * 1024
MAX_PROJECT_DIRECTORY_ENTRIES = 4096
MAX_REGISTRY_INSTALLATIONS = 256
WINDOWS_EDITOR_RELATIVE = Path("Engine/Binaries/Win64/UnrealEditor.exe")


class DeploymentError(RuntimeError):
    """Raised when a deployment input or result is unsafe or unusable."""


def read_json_object(path: Path, label: str) -> dict[str, object]:
    try:
        return read_bounded_json(path, label=label, maximum_bytes=MAX_PROJECT_DESCRIPTOR_BYTES)
    except Exception as error:
        message = str(error).replace(
            f"larger than {MAX_PROJECT_DESCRIPTOR_BYTES} bytes",
            "larger than 1 MiB",
        )
        raise DeploymentError(message) from error


def locate_project(folder: Path) -> ProjectInfo:
    folder = resolved(folder)
    if not folder.is_dir():
        raise DeploymentError(f"project folder does not exist: {folder}")
    descriptors: list[Path] = []
    try:
        for index, path in enumerate(folder.iterdir()):
            if index >= MAX_PROJECT_DIRECTORY_ENTRIES:
                raise DeploymentError(
                    f"project folder contains more than {MAX_PROJECT_DIRECTORY_ENTRIES} entries"
                )
            if path.is_file() and path.suffix.casefold() == ".uproject":
                descriptors.append(path)
    except DeploymentError:
        raise
    except OSError as error:
        raise DeploymentError(f"could not inspect project folder {folder}: {error}") from error
    descriptors.sort(key=lambda path: path.name.casefold())
    if not descriptors:
        raise DeploymentError(f"project folder contains no .uproject file: {folder}")
    if len(descriptors) != 1:
        names = ", ".join(path.name for path in descriptors[:5])
        raise DeploymentError(
            f"project folder must contain exactly one .uproject file; found {len(descriptors)}: {names}"
        )
    descriptor = descriptors[0]
    project = read_json_object(descriptor, "project descriptor")
    association = project.get("EngineAssociation", "")
    if association is None:
        association = ""
    if not isinstance(association, str) or len(association) > 128:
        raise DeploymentError("EngineAssociation must be a string of at most 128 characters")
    return ProjectInfo(folder, descriptor, association.strip())


def registry_installations() -> list[tuple[str, Path]]:
    if platform.system() != "Windows":
        return []
    try:
        import winreg
    except ImportError:
        return []
    installations: list[tuple[str, Path]] = []
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\EpicGames\Unreal Engine", 0, winreg.KEY_READ | getattr(winreg, "KEY_WOW64_64KEY", 0)) as base:
            for index in range(MAX_REGISTRY_INSTALLATIONS):
                try:
                    association = winreg.EnumKey(base, index)
                except OSError:
                    break
                try:
                    with winreg.OpenKey(base, association) as version_key:
                        directory, _ = winreg.QueryValueEx(version_key, "InstalledDirectory")
                    if isinstance(directory, str):
                        installations.append((association, Path(directory)))
                except OSError:
                    continue
    except OSError:
        pass
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"SOFTWARE\Epic Games\Unreal Engine\Builds") as builds:
            for index in range(MAX_REGISTRY_INSTALLATIONS):
                try:
                    association, directory, _ = winreg.EnumValue(builds, index)
                except OSError:
                    break
                if isinstance(directory, str):
                    installations.append((association, Path(directory)))
    except OSError:
        pass
    return installations


def engine_candidates(
    project: ProjectInfo,
    *,
    environment: Mapping[str, str] | None = None,
    installations: Sequence[tuple[str, Path]] | None = None,
) -> list[Path]:
    environment = os.environ if environment is None else environment
    installations = registry_installations() if installations is None else installations
    candidates: list[Path] = []
    configured = environment.get(ENGINE_ROOT_ENV)
    association = project.engine_association
    if association:
        for name, directory in installations:
            if name.casefold() == association.casefold():
                candidates.append(directory)
        for variable in ("ProgramW6432", "ProgramFiles"):
            program_files = environment.get(variable)
            if program_files:
                candidates.append(Path(program_files) / "Epic Games" / f"UE_{association}")
        if configured:
            candidates.append(Path(configured))
    else:
        if configured:
            candidates.append(Path(configured))
        candidates.extend(directory for _, directory in installations)
    unique: list[Path] = []
    identities: set[str] = set()
    for candidate in candidates:
        identity = os.path.normcase(os.path.abspath(os.fspath(candidate)))
        if identity not in identities:
            identities.add(identity)
            unique.append(candidate)
    return unique


def default_engine_root(environment: Mapping[str, str] | None = None) -> str:
    environment = os.environ if environment is None else environment
    return environment.get(ENGINE_ROOT_ENV, "").strip()


def validate_supported_engine_root(engine_root: Path) -> Path:
    try:
        return validate_engine_root(engine_root, "Windows")
    except PackagingError as error:
        raise DeploymentError(str(error)) from error


def resolve_engine_root(project: ProjectInfo, configured: Path | None = None) -> Path:
    if configured is not None:
        candidate = resolved(configured)
        validate_supported_engine_root(candidate)
        return candidate
    for candidate in engine_candidates(project):
        try:
            validate_supported_engine_root(candidate)
        except DeploymentError:
            continue
        return resolved(candidate)
    association = project.engine_association or "<not specified>"
    raise DeploymentError(
        "could not locate the Unreal Engine installation for EngineAssociation "
        f"{association}; select the engine folder manually"
    )


def validate_editor_lifecycle_executable(executable: Path) -> Path:
    if not executable.expanduser().is_absolute():
        raise DeploymentError("editor lifecycle executable must be an absolute path")
    try:
        candidate = executable.expanduser().resolve(strict=True)
    except (OSError, RuntimeError):
        raise DeploymentError("editor lifecycle executable must be an existing regular file") from None
    if not candidate.is_file():
        raise DeploymentError("editor lifecycle executable must be an existing regular file")
    if candidate.name.casefold() != "unrealeditor.exe":
        raise DeploymentError("Windows editor lifecycle executable must be UnrealEditor.exe")
    return candidate


def windows_editor_lifecycle_executable(engine_root: Path) -> Path:
    return validate_editor_lifecycle_executable(resolved(engine_root) / WINDOWS_EDITOR_RELATIVE)
