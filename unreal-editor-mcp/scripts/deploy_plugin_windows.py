#!/usr/bin/env python3
"""Build and install an UnrealMCP binary plugin on Windows."""

from __future__ import annotations

import json
import os
import platform
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Mapping, Sequence

try:
    from scripts import package_plugin
except ModuleNotFoundError:  # Direct execution puts this script's directory on sys.path.
    import package_plugin  # type: ignore[no-redef]


APPLICATION_ROOT = Path(__file__).resolve().parents[1]
SERVER_ENTRY = APPLICATION_ROOT / "server.py"
SERVER_NAME = "unreal-editor"
PLUGIN_NAME = "UnrealMCP"
GAS_PLUGIN_NAME = "UnrealMCPGAS"
INSTALL_IN_PROJECT = "project"
INSTALL_IN_ENGINE_ENABLED = "engine_enabled"
INSTALL_IN_ENGINE_DISABLED = "engine_disabled"
INSTALL_METHODS = frozenset(
    {INSTALL_IN_PROJECT, INSTALL_IN_ENGINE_ENABLED, INSTALL_IN_ENGINE_DISABLED}
)
WINDOWS_EDITOR_RELATIVE = Path("Engine/Binaries/Win64/UnrealEditor.exe")
MAX_PROJECT_DESCRIPTOR_BYTES = 1024 * 1024
MAX_PROJECT_DIRECTORY_ENTRIES = 4096
MAX_MODULE_RULE_BYTES = 64 * 1024
MAX_REGISTRY_INSTALLATIONS = 256
MAX_PROJECT_PLUGIN_REFERENCES = 4096
DEBUG_SUFFIXES = frozenset(
    {".pdb", ".ipdb", ".iobj", ".idb", ".ilk", ".obj", ".pch", ".map", ".debug"}
)
IMPLEMENTATION_SOURCE_SUFFIXES = frozenset({".c", ".cc", ".cpp", ".h", ".hh", ".hpp", ".inl"})
MODULE_RULE_INSERTION_POINT = "        PCHUsage ="
PRECOMPILED_MODULE_RULE = "        bUsePrecompiled = true;\n"
_WINDOWS_REPARSE_POINT = 0x0400


class DeploymentError(RuntimeError):
    """Raised when a deployment input or result is unsafe or unusable."""


@dataclass(frozen=True)
class ProjectInfo:
    folder: Path
    descriptor: Path
    engine_association: str


@dataclass(frozen=True)
class PluginBuild:
    name: str
    descriptor: Path
    dependency_plugins: tuple[Path, ...] = ()


BASE_PLUGIN = PluginBuild(PLUGIN_NAME, package_plugin.PLUGIN_DESCRIPTOR)
GAS_PLUGIN = PluginBuild(
    GAS_PLUGIN_NAME,
    package_plugin.GAS_DESCRIPTOR,
    (package_plugin.PLUGIN_DESCRIPTOR,),
)


def resolved(path: Path) -> Path:
    return path.expanduser().resolve()


def is_within(path: Path, directory: Path) -> bool:
    try:
        path.relative_to(directory)
    except ValueError:
        return False
    return True


def is_reparse_point(path: Path) -> bool:
    try:
        return bool(path.lstat().st_file_attributes & _WINDOWS_REPARSE_POINT)
    except (AttributeError, OSError):
        return path.is_symlink()


def read_json_object(path: Path, label: str) -> dict[str, object]:
    try:
        with path.open("rb") as stream:
            data = stream.read(MAX_PROJECT_DESCRIPTOR_BYTES + 1)
        if len(data) > MAX_PROJECT_DESCRIPTOR_BYTES:
            raise DeploymentError(f"{label} is larger than 1 MiB: {path}")
        value = json.loads(data.decode("utf-8-sig"))
    except DeploymentError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise DeploymentError(f"{label} is not readable JSON: {path}: {error}") from error
    if not isinstance(value, dict):
        raise DeploymentError(f"{label} must contain one JSON object: {path}")
    return value


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
        with winreg.OpenKey(
            winreg.HKEY_LOCAL_MACHINE,
            r"SOFTWARE\EpicGames\Unreal Engine",
            0,
            winreg.KEY_READ | getattr(winreg, "KEY_WOW64_64KEY", 0),
        ) as base:
            index = 0
            while index < MAX_REGISTRY_INSTALLATIONS:
                try:
                    association = winreg.EnumKey(base, index)
                except OSError:
                    break
                index += 1
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
        with winreg.OpenKey(
            winreg.HKEY_CURRENT_USER,
            r"SOFTWARE\Epic Games\Unreal Engine\Builds",
        ) as builds:
            index = 0
            while index < MAX_REGISTRY_INSTALLATIONS:
                try:
                    association, directory, _ = winreg.EnumValue(builds, index)
                except OSError:
                    break
                index += 1
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

    configured = environment.get(package_plugin.ENGINE_ROOT_ENV)
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
    return environment.get(package_plugin.ENGINE_ROOT_ENV, "").strip()


def resolve_engine_root(project: ProjectInfo, configured: Path | None = None) -> Path:
    if configured is not None:
        candidate = resolved(configured)
        try:
            validate_supported_engine_root(candidate)
        except (package_plugin.PackagingError, DeploymentError) as error:
            raise DeploymentError(str(error)) from error
        return candidate

    for candidate in engine_candidates(project):
        try:
            validate_supported_engine_root(candidate)
        except (package_plugin.PackagingError, DeploymentError):
            continue
        return resolved(candidate)
    association = project.engine_association or "<not specified>"
    raise DeploymentError(
        "could not locate the Unreal Engine installation for EngineAssociation "
        f"{association}; select the engine folder manually"
    )


def validate_supported_engine_root(engine_root: Path) -> Path:
    run_uat = package_plugin.validate_engine_root(engine_root, "Windows")
    version_file = resolved(engine_root) / "Engine" / "Build" / "Build.version"
    version = read_json_object(version_file, "Unreal Engine build version")
    major = version.get("MajorVersion")
    minor = version.get("MinorVersion")
    if type(major) is not int or type(minor) is not int:
        raise DeploymentError(f"Unreal Engine build version has invalid major/minor fields: {version_file}")
    if (major, minor) < (5, 8):
        raise DeploymentError(
            f"Unreal MCP requires Unreal Engine 5.8 or newer; selected Engine is {major}.{minor}"
        )
    return run_uat


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


def build_command(
    engine_root: Path,
    output: Path,
    plugin: PluginBuild = BASE_PLUGIN,
) -> list[str]:
    try:
        run_uat = validate_supported_engine_root(engine_root)
        output = package_plugin.validate_output(output, engine_root, plugin.descriptor)
    except (package_plugin.PackagingError, DeploymentError) as error:
        raise DeploymentError(str(error)) from error
    return package_plugin.build_command(
        run_uat,
        output,
        "Win64",
        strict_includes=False,
        unversioned=False,
        plugin_descriptor=plugin.descriptor,
        dependency_plugins=plugin.dependency_plugins,
    )


def run_packaging(
    engine_root: Path,
    output: Path,
    log: Callable[[str], None],
    plugin: PluginBuild = BASE_PLUGIN,
) -> None:
    command = build_command(engine_root, output, plugin)
    log(f"Building installed Win64 {plugin.name} plugin with {engine_root}")
    creation_flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    try:
        process = subprocess.Popen(
            command,
            cwd=package_plugin.WORKSPACE_ROOT,
            env=os.environ.copy(),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            creationflags=creation_flags,
        )
    except OSError as error:
        raise DeploymentError(f"could not start Unreal AutomationTool: {error}") from error
    assert process.stdout is not None
    for line in process.stdout:
        log(line.rstrip())
    return_code = process.wait()
    if return_code != 0:
        raise DeploymentError(f"Unreal AutomationTool failed with exit code {return_code}")
    try:
        package_plugin.restore_source_descriptor_contract(output, plugin.descriptor)
        package_plugin.verify_package(output, plugin.descriptor)
    except package_plugin.PackagingError as error:
        raise DeploymentError(str(error)) from error


def ignored_binary_items(
    directory: str,
    names: list[str],
    *,
    include_pdb: bool = False,
    plugin_name: str = PLUGIN_NAME,
) -> set[str]:
    current = Path(directory)
    is_win64_binary_root = (
        current.name.casefold() == "win64"
        and current.parent.name.casefold() == "binaries"
    )
    dll_stems = {
        Path(name).stem.casefold()
        for name in names
        if Path(name).suffix.casefold() == ".dll"
    }
    ignored: set[str] = set()
    for name in names:
        lowered = name.casefold()
        suffix = Path(name).suffix.casefold()
        is_module_source_root = (
            current.name.casefold() == plugin_name.casefold()
            and current.parent.name.casefold() == "source"
        )
        if is_module_source_root and lowered != f"{plugin_name.casefold()}.build.cs":
            ignored.add(name)
        elif lowered.endswith(".dsym"):
            ignored.add(name)
        elif suffix in DEBUG_SUFFIXES and not (
            include_pdb
            and suffix == ".pdb"
            and is_win64_binary_root
            and Path(name).stem.casefold() in dll_stems
        ):
            ignored.add(name)
    return ignored


def verify_binary_plugin(
    plugin_root: Path,
    *,
    include_pdb: bool = False,
    plugin_name: str = PLUGIN_NAME,
    enabled_by_default: bool | None = None,
) -> None:
    descriptor = plugin_root / f"{plugin_name}.uplugin"
    value = read_json_object(descriptor, "installed plugin descriptor")
    if value.get("Installed") is not True:
        raise DeploymentError("installed plugin descriptor is not marked Installed")
    if enabled_by_default is not None and value.get("EnabledByDefault") is not enabled_by_default:
        raise DeploymentError(
            f"installed {plugin_name} descriptor does not set EnabledByDefault "
            f"to {str(enabled_by_default).lower()}"
        )
    module_rules = plugin_root / "Source" / plugin_name / f"{plugin_name}.Build.cs"
    if not module_rules.is_file():
        raise DeploymentError(f"binary deployment is missing Unreal Build Tool module rules: {module_rules}")
    try:
        rules_text = read_bounded_module_rules(module_rules)
    except (OSError, UnicodeError) as error:
        raise DeploymentError(f"binary module rules are unreadable: {module_rules}: {error}") from error
    if PRECOMPILED_MODULE_RULE.strip() not in rules_text:
        raise DeploymentError("binary module rules do not require the packaged precompiled module")
    implementation_source = next(
        (
            path
            for path in plugin_root.rglob("*")
            if path.is_file() and path.suffix.casefold() in IMPLEMENTATION_SOURCE_SUFFIXES
        ),
        None,
    )
    if implementation_source is not None:
        raise DeploymentError(
            f"binary deployment unexpectedly contains implementation source: {implementation_source}"
        )
    binary_root = plugin_root / "Binaries" / "Win64"
    binary_dlls = (
        [
            path
            for path in binary_root.iterdir()
            if path.is_file() and path.suffix.casefold() == ".dll"
        ]
        if binary_root.is_dir()
        else []
    )
    if not binary_dlls:
        raise DeploymentError(f"binary deployment contains no Win64 plugin DLL: {binary_root}")
    binary_dll_stems = {path.stem.casefold() for path in binary_dlls}
    binary_pdbs = [
        path
        for path in binary_root.iterdir()
        if path.is_file() and path.suffix.casefold() == ".pdb"
    ]
    matching_pdb_stems = {
        path.stem.casefold()
        for path in binary_pdbs
        if path.stem.casefold() in binary_dll_stems
    }
    if include_pdb and matching_pdb_stems != binary_dll_stems:
        missing = sorted(binary_dll_stems - matching_pdb_stems)
        raise DeploymentError(
            "binary deployment is missing matching Win64 PDB crash symbols for: "
            + ", ".join(missing)
        )
    precompiled_root = plugin_root / "Intermediate" / "Build" / "Win64"
    if not precompiled_root.is_dir() or not any(
        path.is_file() and path.suffix.casefold() == ".lib"
        for path in precompiled_root.rglob("*")
    ):
        raise DeploymentError(
            f"binary deployment contains no Win64 precompiled import library: {precompiled_root}"
        )
    debug_artifact = next(
        (
            path
            for path in plugin_root.rglob("*")
            if path.is_file()
            and path.suffix.casefold() in DEBUG_SUFFIXES
            and not (
                include_pdb
                and path.parent == binary_root
                and path.suffix.casefold() == ".pdb"
                and path.stem.casefold() in binary_dll_stems
            )
        ),
        None,
    )
    if debug_artifact is not None:
        raise DeploymentError(f"binary deployment still contains a debug artifact: {debug_artifact}")


def configure_precompiled_module_rules(
    plugin_root: Path,
    plugin_name: str = PLUGIN_NAME,
) -> None:
    module_rules = plugin_root / "Source" / plugin_name / f"{plugin_name}.Build.cs"
    try:
        rules_text = read_bounded_module_rules(module_rules)
    except (OSError, UnicodeError) as error:
        raise DeploymentError(f"packaged module rules are unreadable: {module_rules}: {error}") from error
    if PRECOMPILED_MODULE_RULE.strip() in rules_text:
        return
    if rules_text.count(MODULE_RULE_INSERTION_POINT) != 1:
        raise DeploymentError(
            "packaged module rules do not contain the expected single PCHUsage assignment"
        )
    configured = rules_text.replace(
        MODULE_RULE_INSERTION_POINT,
        PRECOMPILED_MODULE_RULE + MODULE_RULE_INSERTION_POINT,
        1,
    )
    try:
        module_rules.write_text(configured, encoding="utf-8", newline="\n")
    except OSError as error:
        raise DeploymentError(f"could not configure precompiled module rules: {error}") from error


def read_bounded_module_rules(module_rules: Path) -> str:
    with module_rules.open("rb") as stream:
        data = stream.read(MAX_MODULE_RULE_BYTES + 1)
    if len(data) > MAX_MODULE_RULE_BYTES:
        raise DeploymentError(f"module rules are larger than 64 KiB: {module_rules}")
    return data.decode("utf-8")


def _plugin_destination(parent: Path, root: Path, plugin_name: str, label: str) -> Path:
    if parent.exists() and is_reparse_point(parent):
        raise DeploymentError(f"refusing to install through a reparse-point {label}: {parent}")
    destination = parent / plugin_name
    if destination.exists() and is_reparse_point(destination):
        raise DeploymentError(f"refusing to replace a reparse-point plugin directory: {destination}")
    resolved_destination = resolved(parent) / plugin_name
    if not is_within(resolved_destination, resolved(root)):
        raise DeploymentError(f"plugin destination escapes the selected {label}")
    return destination


def plugin_destination(
    project: ProjectInfo,
    plugin_name: str = PLUGIN_NAME,
) -> Path:
    plugins = project.folder / "Plugins"
    return _plugin_destination(plugins, project.folder, plugin_name, "project Plugins directory")


def engine_plugin_destination(engine_root: Path, plugin_name: str = PLUGIN_NAME) -> Path:
    engine_root = resolved(engine_root)
    plugins = engine_root / "Engine" / "Plugins"
    marketplace = plugins / "Marketplace"
    if plugins.exists() and is_reparse_point(plugins):
        raise DeploymentError(f"refusing to install through a reparse-point Engine Plugins directory: {plugins}")
    return _plugin_destination(marketplace, engine_root, plugin_name, "Engine Plugins directory")


def validate_install_method(install_method: str) -> str:
    if install_method not in INSTALL_METHODS:
        raise DeploymentError(f"unsupported install method: {install_method!r}")
    return install_method


def deployment_destinations(
    project: ProjectInfo,
    engine_root: Path,
    install_method: str,
    *,
    include_gas: bool,
) -> tuple[Path, ...]:
    validate_install_method(install_method)
    if type(include_gas) is not bool:
        raise DeploymentError("include_gas must be Boolean")
    names = (PLUGIN_NAME, GAS_PLUGIN_NAME) if include_gas else (PLUGIN_NAME,)
    if install_method == INSTALL_IN_PROJECT:
        return tuple(plugin_destination(project, name) for name in names)
    return tuple(engine_plugin_destination(engine_root, name) for name in names)


def project_descriptor_update(
    project: ProjectInfo,
    plugin_names: Sequence[str],
) -> tuple[bytes, bytes]:
    try:
        with project.descriptor.open("rb") as stream:
            original = stream.read(MAX_PROJECT_DESCRIPTOR_BYTES + 1)
        if len(original) > MAX_PROJECT_DESCRIPTOR_BYTES:
            raise DeploymentError(f"project descriptor is larger than 1 MiB: {project.descriptor}")
        value = json.loads(original.decode("utf-8-sig"))
    except DeploymentError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise DeploymentError(
            f"project descriptor is not readable JSON: {project.descriptor}: {error}"
        ) from error
    if not isinstance(value, dict):
        raise DeploymentError(f"project descriptor must contain one JSON object: {project.descriptor}")
    references = value.get("Plugins")
    if references is None:
        references = []
        value["Plugins"] = references
    if not isinstance(references, list):
        raise DeploymentError("project descriptor Plugins must be an array")
    if len(references) > MAX_PROJECT_PLUGIN_REFERENCES:
        raise DeploymentError(
            f"project descriptor contains more than {MAX_PROJECT_PLUGIN_REFERENCES} plugin references"
        )
    requested = {name.casefold(): name for name in plugin_names}
    found: set[str] = set()
    for reference in references:
        if not isinstance(reference, dict):
            raise DeploymentError("project descriptor plugin references must be objects")
        name = reference.get("Name")
        if not isinstance(name, str):
            raise DeploymentError("project descriptor plugin reference Name must be a string")
        key = name.casefold()
        if key in requested:
            if key in found:
                raise DeploymentError(f"project descriptor contains duplicate {requested[key]} references")
            reference["Enabled"] = True
            found.add(key)
    for key, name in requested.items():
        if key not in found:
            references.append({"Name": name, "Enabled": True})
    if len(references) > MAX_PROJECT_PLUGIN_REFERENCES:
        raise DeploymentError(
            f"enabled project descriptor would contain more than "
            f"{MAX_PROJECT_PLUGIN_REFERENCES} plugin references"
        )
    encoded = (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    if len(encoded) > MAX_PROJECT_DESCRIPTOR_BYTES:
        raise DeploymentError("enabled project descriptor would be larger than 1 MiB")
    return original, encoded


def configured_project_descriptor(project: ProjectInfo, plugin_names: Sequence[str]) -> bytes:
    return project_descriptor_update(project, plugin_names)[1]


def write_project_descriptor(
    project: ProjectInfo,
    encoded: bytes,
    *,
    expected_original: bytes | None = None,
) -> None:
    descriptor = project.descriptor
    if is_reparse_point(descriptor) or resolved(descriptor).parent != resolved(project.folder):
        raise DeploymentError(f"refusing to update an indirect project descriptor: {descriptor}")
    if expected_original is not None:
        try:
            with descriptor.open("rb") as stream:
                current = stream.read(MAX_PROJECT_DESCRIPTOR_BYTES + 1)
        except OSError as error:
            raise DeploymentError(f"could not re-read project descriptor: {descriptor}: {error}") from error
        if current != expected_original:
            raise DeploymentError(
                f"project descriptor changed while plugins were building; refusing to overwrite it: {descriptor}"
            )
    temporary = descriptor.parent / f".{descriptor.name}.unreal-mcp-{uuid.uuid4().hex}.tmp"
    try:
        with temporary.open("xb") as stream:
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, descriptor)
    except OSError as error:
        raise DeploymentError(f"could not enable plugins in {descriptor}: {error}") from error
    finally:
        if temporary.exists():
            temporary.unlink(missing_ok=True)


def configure_installed_descriptor(
    plugin_root: Path,
    plugin_name: str,
    enabled_by_default: bool | None,
) -> None:
    if enabled_by_default is not None and type(enabled_by_default) is not bool:
        raise DeploymentError("enabled_by_default must be Boolean or null")
    if enabled_by_default is None:
        return
    descriptor = plugin_root / f"{plugin_name}.uplugin"
    value = read_json_object(descriptor, "installed plugin descriptor")
    value["EnabledByDefault"] = enabled_by_default
    encoded = (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    if len(encoded) > MAX_PROJECT_DESCRIPTOR_BYTES:
        raise DeploymentError(f"configured installed plugin descriptor is larger than 1 MiB: {descriptor}")
    try:
        descriptor.write_bytes(encoded)
    except OSError as error:
        raise DeploymentError(f"could not configure installed plugin descriptor: {descriptor}: {error}") from error


def install_binary_plugins(
    packages: Sequence[tuple[PluginBuild, Path, Path]],
    *,
    replace_existing: bool,
    include_pdb: bool = False,
    enabled_by_default: bool | None = None,
    after_install: Callable[[], None] | None = None,
) -> tuple[Path, ...]:
    if type(replace_existing) is not bool:
        raise DeploymentError("replace_existing must be Boolean")
    if type(include_pdb) is not bool:
        raise DeploymentError("include_pdb must be Boolean")
    if not packages or len(packages) > 2:
        raise DeploymentError("deployment must contain one or two plugins")
    names = [plugin.name.casefold() for plugin, _, _ in packages]
    if len(names) != len(set(names)):
        raise DeploymentError("deployment plugin names must be unique")

    nonce = uuid.uuid4().hex
    staged: list[tuple[PluginBuild, Path, Path, Path]] = []
    installed: list[tuple[PluginBuild, Path, Path]] = []
    try:
        for plugin, package_root, destination in packages:
            if destination.exists() and not destination.is_dir():
                raise DeploymentError(f"plugin destination exists and is not a directory: {destination}")
            if destination.exists() and not replace_existing:
                raise DeploymentError(f"plugin is already installed: {destination}")
            destination.parent.mkdir(parents=True, exist_ok=True)
            staging = destination.parent / f".{plugin.name}.install-{nonce}"
            backup = destination.parent / f".{plugin.name}.backup-{nonce}"
            staged.append((plugin, staging, destination, backup))
            shutil.copytree(
                package_root,
                staging,
                ignore=lambda directory, items, name=plugin.name: ignored_binary_items(
                    directory,
                    items,
                    include_pdb=include_pdb,
                    plugin_name=name,
                ),
            )
            configure_precompiled_module_rules(staging, plugin.name)
            configure_installed_descriptor(staging, plugin.name, enabled_by_default)
            verify_binary_plugin(
                staging,
                include_pdb=include_pdb,
                plugin_name=plugin.name,
                enabled_by_default=enabled_by_default,
            )

        for plugin, staging, destination, backup in staged:
            if destination.exists():
                destination.rename(backup)
            try:
                staging.rename(destination)
            except BaseException:
                if backup.exists():
                    backup.rename(destination)
                raise
            installed.append((plugin, destination, backup))
            verify_binary_plugin(
                destination,
                include_pdb=include_pdb,
                plugin_name=plugin.name,
                enabled_by_default=enabled_by_default,
            )
        if after_install is not None:
            after_install()
        for _, _, backup in installed:
            if backup.exists():
                shutil.rmtree(backup, ignore_errors=True)
    except BaseException as error:
        for _, destination, backup in reversed(installed):
            if destination.exists():
                shutil.rmtree(destination, ignore_errors=True)
            if backup.exists():
                backup.rename(destination)
        if isinstance(error, DeploymentError):
            raise
        if isinstance(error, OSError):
            raise DeploymentError(f"could not install selected plugins: {error}") from error
        raise
    finally:
        for _, staging, _, _ in staged:
            if staging.exists():
                shutil.rmtree(staging, ignore_errors=True)
    return tuple(destination for _, destination, _ in installed)


def install_binary_plugin(
    package_root: Path,
    project: ProjectInfo,
    *,
    replace_existing: bool,
    include_pdb: bool = False,
) -> Path:
    destination = plugin_destination(project)
    return install_binary_plugins(
        ((BASE_PLUGIN, package_root, destination),),
        replace_existing=replace_existing,
        include_pdb=include_pdb,
    )[0]


def mcp_server_definition(
    project: ProjectInfo,
    python_executable: Path | None = None,
    *,
    writable: bool = False,
    editor_lifecycle: Path | None = None,
) -> dict[str, object]:
    if type(writable) is not bool:
        raise DeploymentError("writable must be Boolean")
    executable = resolved(Path(sys.executable) if python_executable is None else python_executable)
    arguments = [str(SERVER_ENTRY), str(project.descriptor)]
    if writable:
        arguments.append("--writable")
    if editor_lifecycle is not None:
        lifecycle_executable = validate_editor_lifecycle_executable(editor_lifecycle)
        arguments.extend(("--editor-lifecycle", str(lifecycle_executable)))
    return {"command": str(executable), "args": arguments}


def format_lm_studio_json(definition: Mapping[str, object]) -> str:
    return json.dumps(
        {"mcpServers": {SERVER_NAME: dict(definition)}},
        indent=2,
        ensure_ascii=False,
    )


def lm_studio_json(
    project: ProjectInfo,
    python_executable: Path | None = None,
    *,
    writable: bool = False,
    editor_lifecycle: Path | None = None,
) -> str:
    definition = mcp_server_definition(
        project,
        python_executable,
        writable=writable,
        editor_lifecycle=editor_lifecycle,
    )
    return format_lm_studio_json(definition)


def deploy(
    project: ProjectInfo,
    engine_root: Path,
    *,
    replace_existing: bool,
    include_pdb: bool = False,
    include_gas: bool = False,
    install_method: str = INSTALL_IN_PROJECT,
    log: Callable[[str], None],
) -> tuple[Path, ...]:
    if type(replace_existing) is not bool:
        raise DeploymentError("replace_existing must be Boolean")
    validate_install_method(install_method)
    if type(include_gas) is not bool:
        raise DeploymentError("include_gas must be Boolean")
    plugins = (BASE_PLUGIN, GAS_PLUGIN) if include_gas else (BASE_PLUGIN,)
    destinations = deployment_destinations(
        project,
        engine_root,
        install_method,
        include_gas=include_gas,
    )
    existing_before_build = tuple(destination for destination in destinations if destination.exists())
    if existing_before_build and not replace_existing:
        raise DeploymentError(
            "selected plugin installation already exists: "
            + ", ".join(str(destination) for destination in existing_before_build)
        )
    enabled_by_default = (
        True if install_method == INSTALL_IN_ENGINE_ENABLED
        else False if install_method == INSTALL_IN_ENGINE_DISABLED
        else None
    )
    project_descriptor = (
        project_descriptor_update(project, [plugin.name for plugin in plugins])
        if install_method == INSTALL_IN_PROJECT
        else None
    )
    with tempfile.TemporaryDirectory(prefix="unreal-mcp-package-") as temporary:
        package_roots: list[Path] = []
        for plugin in plugins:
            package_root = Path(temporary) / plugin.name
            run_packaging(engine_root, package_root, log, plugin)
            package_roots.append(package_root)
        if include_pdb:
            log("Removing implementation source and debug artifacts except matching Win64 PDBs")
        else:
            log("Removing implementation source and debug-symbol artifacts")
        existing_after_build = tuple(
            destination for destination in destinations if destination.exists()
        )
        if existing_after_build != existing_before_build:
            raise DeploymentError(
                "selected plugin installation state changed while packages were building"
            )
        installed = install_binary_plugins(
            tuple(zip(plugins, package_roots, destinations)),
            replace_existing=replace_existing,
            include_pdb=include_pdb,
            enabled_by_default=enabled_by_default,
            after_install=(
                (
                    lambda: write_project_descriptor(
                        project,
                        project_descriptor[1],
                        expected_original=project_descriptor[0],
                    )
                )
                if project_descriptor is not None
                else None
            ),
        )
    for destination in installed:
        log(f"Installed binary plugin at {destination}")
    return installed


class DeploymentWindow:
    def __init__(self) -> None:
        import tkinter as tk
        from tkinter import ttk

        self.tk = tk
        self.ttk = ttk
        self.root = tk.Tk()
        self.root.title("Unreal MCP — Windows Deployment")
        self.root.geometry("820x830")
        self.root.minsize(680, 730)
        self.events: queue.Queue[tuple[str, object]] = queue.Queue()
        self.project_value = tk.StringVar()
        self.engine_value = tk.StringVar(value=default_engine_root())
        self.include_gas_value = tk.BooleanVar(value=False)
        self.include_pdb_value = tk.BooleanVar(value=False)
        self.install_method_value = tk.StringVar(value=INSTALL_IN_PROJECT)
        self.writable_value = tk.BooleanVar(value=False)
        self.lifecycle_value = tk.BooleanVar(value=False)
        self.server_name_value = tk.StringVar(value=SERVER_NAME)
        self.command_value = tk.StringVar()
        self.argument_values = [tk.StringVar() for _ in range(5)]
        self.configuration_copy_buttons: list[object] = []
        self.status_value = tk.StringVar(value="Select the folder containing your .uproject file.")
        self.busy = False
        self._build()
        self.root.protocol("WM_DELETE_WINDOW", self._close)
        self.root.after(100, self._poll_events)

    def _build(self) -> None:
        from tkinter import scrolledtext

        frame = self.ttk.Frame(self.root, padding=14)
        frame.pack(fill="both", expand=True)
        frame.columnconfigure(1, weight=1)
        frame.rowconfigure(10, weight=1)
        frame.rowconfigure(12, weight=1)

        self.ttk.Label(frame, text="Unreal project folder").grid(row=0, column=0, sticky="w")
        self.project_entry = self.ttk.Entry(frame, textvariable=self.project_value)
        self.project_entry.grid(row=0, column=1, sticky="ew", padx=8)
        self.project_button = self.ttk.Button(frame, text="Browse…", command=self._browse_project)
        self.project_button.grid(row=0, column=2)

        self.ttk.Label(frame, text="Unreal Engine folder").grid(
            row=1, column=0, sticky="w", pady=(10, 0)
        )
        self.engine_entry = self.ttk.Entry(frame, textvariable=self.engine_value)
        self.engine_entry.grid(row=1, column=1, sticky="ew", padx=8, pady=(10, 0))
        self.engine_button = self.ttk.Button(frame, text="Browse…", command=self._browse_engine)
        self.engine_button.grid(row=1, column=2, pady=(10, 0))

        self.ttk.Label(
            frame,
            text="Close Unreal Editor before installing. The build uses the selected Engine and Visual Studio.",
            wraplength=760,
        ).grid(row=2, column=0, columnspan=3, sticky="w", pady=(12, 0))
        self.include_gas_checkbox = self.ttk.Checkbutton(
            frame,
            text="Build and install Unreal MCP GAS companion plugin",
            variable=self.include_gas_value,
        )
        self.include_gas_checkbox.grid(
            row=3,
            column=0,
            columnspan=3,
            sticky="w",
            pady=(10, 0),
        )
        self.include_pdb_checkbox = self.ttk.Checkbutton(
            frame,
            text="Include matching PDB crash symbols (larger installation)",
            variable=self.include_pdb_value,
        )
        self.include_pdb_checkbox.grid(
            row=4,
            column=0,
            columnspan=3,
            sticky="w",
            pady=(8, 0),
        )

        install_methods = self.ttk.LabelFrame(frame, text="Install method", padding=8)
        install_methods.grid(row=5, column=0, columnspan=3, sticky="ew", pady=(10, 0))
        self.install_method_buttons = (
            self.ttk.Radiobutton(
                install_methods,
                text="Install into project (and enable)",
                variable=self.install_method_value,
                value=INSTALL_IN_PROJECT,
            ),
            self.ttk.Radiobutton(
                install_methods,
                text="Install into engine (and set enabled by default)",
                variable=self.install_method_value,
                value=INSTALL_IN_ENGINE_ENABLED,
            ),
            self.ttk.Radiobutton(
                install_methods,
                text="Install into engine (without enabling by default)",
                variable=self.install_method_value,
                value=INSTALL_IN_ENGINE_DISABLED,
            ),
        )
        for row, button in enumerate(self.install_method_buttons):
            button.grid(row=row, column=0, sticky="w", pady=(0 if row == 0 else 4, 0))
        self.writable_checkbox = self.ttk.Checkbutton(
            frame,
            text="Enable writable MCP tools in the generated LM Studio entry",
            variable=self.writable_value,
        )
        self.writable_checkbox.grid(row=6, column=0, columnspan=3, sticky="w", pady=(8, 0))
        self.lifecycle_checkbox = self.ttk.Checkbutton(
            frame,
            text="Enable editor lifecycle control using the selected Engine",
            variable=self.lifecycle_value,
        )
        self.lifecycle_checkbox.grid(row=7, column=0, columnspan=3, sticky="w", pady=(8, 0))
        self.install_button = self.ttk.Button(
            frame,
            text="Build and install selected plugins",
            command=self._install,
        )
        self.install_button.grid(row=8, column=0, columnspan=3, sticky="ew", pady=12)
        self.ttk.Label(frame, textvariable=self.status_value, wraplength=760).grid(
            row=9, column=0, columnspan=3, sticky="w"
        )

        self.log_text = scrolledtext.ScrolledText(frame, height=12, state="disabled", wrap="word")
        self.log_text.grid(row=10, column=0, columnspan=3, sticky="nsew", pady=(8, 12))

        self.ttk.Label(frame, text="MCP configuration preview").grid(
            row=11, column=0, columnspan=3, sticky="w"
        )
        previews = self.ttk.Notebook(frame)
        previews.grid(row=12, column=0, columnspan=3, sticky="nsew", pady=(8, 0))

        lm_studio = self.ttk.Frame(previews, padding=10)
        lm_studio.columnconfigure(0, weight=1)
        lm_studio.rowconfigure(1, weight=1)
        previews.add(lm_studio, text="LM Studio JSON")
        json_header = self.ttk.Frame(lm_studio)
        json_header.grid(row=0, column=0, sticky="ew", pady=(0, 8))
        json_header.columnconfigure(0, weight=1)
        self.ttk.Label(json_header, text="Complete mcpServers object").grid(
            row=0, column=0, sticky="w"
        )
        self.copy_button = self.ttk.Button(
            json_header, text="Copy JSON", command=self._copy_json, state="disabled"
        )
        self.copy_button.grid(row=0, column=1, sticky="e")
        self.json_text = scrolledtext.ScrolledText(
            lm_studio, height=11, state="disabled", wrap="none"
        )
        self.json_text.grid(row=1, column=0, sticky="nsew")

        codex = self.ttk.Frame(previews, padding=10)
        codex.columnconfigure(1, weight=1)
        previews.add(codex, text="ChatGPT Codex STDIO")
        self._copyable_row(codex, 0, "Name", self.server_name_value)
        self._copyable_row(codex, 1, "Command / Python", self.command_value)
        for index, value in enumerate(self.argument_values, start=1):
            self._copyable_row(codex, index + 1, f"Argument {index}", value)

    def _copyable_row(self, parent: object, row: int, label: str, value: object) -> None:
        self.ttk.Label(parent, text=label).grid(
            row=row, column=0, sticky="w", padx=(0, 10), pady=2
        )
        self.ttk.Entry(parent, textvariable=value, state="readonly").grid(
            row=row, column=1, sticky="ew", pady=2
        )
        button = self.ttk.Button(
            parent,
            text="Copy",
            command=lambda item=value: self._copy_value(item.get()),
            state="disabled",
        )
        button.grid(row=row, column=2, padx=(10, 0), pady=2)
        self.configuration_copy_buttons.append(button)

    def _set_configuration(self, configuration: str, definition: Mapping[str, object]) -> None:
        self.json_text.configure(state="normal")
        self.json_text.delete("1.0", "end")
        self.json_text.insert("1.0", configuration)
        self.json_text.configure(state="disabled")
        self.command_value.set(str(definition["command"]))
        arguments = definition["args"]
        assert isinstance(arguments, list)
        for index, value in enumerate(self.argument_values):
            value.set(str(arguments[index]) if index < len(arguments) else "")
        copy_values = [
            self.server_name_value.get(),
            self.command_value.get(),
            *(value.get() for value in self.argument_values),
        ]
        for button, value in zip(self.configuration_copy_buttons, copy_values):
            button.configure(state="normal" if value else "disabled")

    def _clear_configuration(self) -> None:
        self.json_text.configure(state="normal")
        self.json_text.delete("1.0", "end")
        self.json_text.configure(state="disabled")
        self.command_value.set("")
        for value in self.argument_values:
            value.set("")
        for button in self.configuration_copy_buttons:
            button.configure(state="disabled")

    def _browse_project(self) -> None:
        from tkinter import filedialog, messagebox

        selected = filedialog.askdirectory(title="Select the Unreal project folder", mustexist=True)
        if not selected:
            return
        self.project_value.set(selected)
        try:
            project = locate_project(Path(selected))
            self.status_value.set(f"Selected {project.descriptor.name}")
            try:
                configured_text = self.engine_value.get().strip()
                configured = Path(configured_text) if configured_text else None
                engine = resolve_engine_root(project, configured)
            except DeploymentError:
                self.engine_value.set("")
                try:
                    engine = resolve_engine_root(project)
                except DeploymentError:
                    return
            else:
                self.engine_value.set(str(engine))
                self.status_value.set(
                    f"Selected {project.descriptor.name}; detected Engine at {engine}"
                )
                return
            self.engine_value.set(str(engine))
            self.status_value.set(
                f"Selected {project.descriptor.name}; detected Engine at {engine}"
            )
        except DeploymentError as error:
            messagebox.showerror("Invalid Unreal project", str(error))

    def _browse_engine(self) -> None:
        from tkinter import filedialog, messagebox

        selected = filedialog.askdirectory(title="Select the Unreal Engine installation", mustexist=True)
        if not selected:
            return
        try:
            project = locate_project(Path(self.project_value.get()))
            engine = resolve_engine_root(project, Path(selected))
        except DeploymentError as error:
            messagebox.showerror("Invalid Unreal Engine folder", str(error))
            return
        self.engine_value.set(str(engine))
        self.status_value.set(f"Using Engine at {engine}")

    def _set_busy(self, busy: bool) -> None:
        self.busy = busy
        state = "disabled" if busy else "normal"
        for widget in (
            self.project_entry,
            self.project_button,
            self.engine_entry,
            self.engine_button,
            self.include_gas_checkbox,
            self.include_pdb_checkbox,
            self.writable_checkbox,
            self.lifecycle_checkbox,
            self.install_button,
            *self.install_method_buttons,
        ):
            widget.configure(state=state)

    def _append_log(self, message: str) -> None:
        self.log_text.configure(state="normal")
        self.log_text.insert("end", message + "\n")
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    def _install(self) -> None:
        from tkinter import messagebox

        try:
            project = locate_project(Path(self.project_value.get()))
            configured = Path(self.engine_value.get()) if self.engine_value.get().strip() else None
            engine = resolve_engine_root(project, configured)
            editor_lifecycle = (
                windows_editor_lifecycle_executable(engine)
                if bool(self.lifecycle_value.get())
                else None
            )
            include_gas = bool(self.include_gas_value.get())
            install_method = validate_install_method(self.install_method_value.get())
            destinations = deployment_destinations(
                project,
                engine,
                install_method,
                include_gas=include_gas,
            )
        except DeploymentError as error:
            messagebox.showerror("Cannot install Unreal MCP", str(error))
            return
        existing = tuple(destination for destination in destinations if destination.exists())
        replace_existing = bool(existing)
        include_pdb = bool(self.include_pdb_value.get())
        writable = bool(self.writable_value.get())
        if replace_existing and not messagebox.askyesno(
            "Replace existing plugins?",
            "These plugin installations already exist:\n\n"
            + "\n".join(str(destination) for destination in existing)
            + "\n\nReplace them with newly built binary plugins?",
        ):
            return

        self._set_busy(True)
        self.copy_button.configure(state="disabled")
        self._clear_configuration()
        self.status_value.set("Building Unreal MCP. This can take several minutes…")
        self.log_text.configure(state="normal")
        self.log_text.delete("1.0", "end")
        self.log_text.configure(state="disabled")

        def worker() -> None:
            try:
                destination_paths = deploy(
                    project,
                    engine,
                    replace_existing=replace_existing,
                    include_pdb=include_pdb,
                    include_gas=include_gas,
                    install_method=install_method,
                    log=lambda message: self.events.put(("log", message)),
                )
                definition = mcp_server_definition(
                    project,
                    writable=writable,
                    editor_lifecycle=editor_lifecycle,
                )
                result = (
                    destination_paths,
                    format_lm_studio_json(definition),
                    definition,
                )
                self.events.put(("done", result))
            except Exception as error:
                self.events.put(("error", str(error)))

        threading.Thread(target=worker, name="UnrealMCPDeployment", daemon=True).start()

    def _poll_events(self) -> None:
        from tkinter import messagebox

        try:
            while True:
                kind, payload = self.events.get_nowait()
                if kind == "log":
                    self._append_log(str(payload))
                elif kind == "done":
                    destinations, configuration, definition = payload  # type: ignore[misc]
                    self._set_configuration(configuration, definition)
                    self.copy_button.configure(state="normal")
                    self._set_busy(False)
                    self.status_value.set(
                        "Installation complete. Open the project, then copy the JSON into LM Studio."
                    )
                    messagebox.showinfo(
                        "Unreal MCP installed",
                        "Installed at:\n"
                        + "\n".join(str(destination) for destination in destinations)
                        + "\n\nThe LM Studio JSON is ready to copy.",
                    )
                elif kind == "error":
                    self._set_busy(False)
                    self.status_value.set("Installation failed. Review the build log and try again.")
                    messagebox.showerror("Unreal MCP installation failed", str(payload))
        except queue.Empty:
            pass
        self.root.after(100, self._poll_events)

    def _copy_json(self) -> None:
        self._copy_value(self.json_text.get("1.0", "end-1c"))

    def _copy_value(self, value: str) -> None:
        if not value:
            self.status_value.set("Install the selected plugins before copying configuration.")
            return
        self.root.clipboard_clear()
        self.root.clipboard_append(value)
        self.root.update()
        self.status_value.set("Configuration value copied to the clipboard.")

    def _close(self) -> None:
        if self.busy:
            from tkinter import messagebox

            messagebox.showwarning(
                "Deployment in progress",
                "Wait for the Unreal plugin build and installation to finish before closing this window.",
            )
            return
        self.root.destroy()

    def run(self) -> None:
        self.root.mainloop()


def main() -> int:
    if platform.system() != "Windows":
        print("This graphical deployment helper is supported only on Windows.", file=sys.stderr)
        return 1
    try:
        DeploymentWindow().run()
    except ImportError as error:
        print(f"Tkinter is required for the graphical deployment helper: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
