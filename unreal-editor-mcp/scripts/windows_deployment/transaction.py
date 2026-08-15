"""Root-confined selected-plugin installation transaction."""

from __future__ import annotations

import json
import os
import shutil
import uuid
from pathlib import Path
from typing import Callable, Sequence

try:
    from scripts.unreal_tooling.paths import is_reparse_point, is_within, resolved
except ModuleNotFoundError:
    from unreal_tooling.paths import is_reparse_point, is_within, resolved  # type: ignore[no-redef]

from .discovery import DeploymentError, MAX_PROJECT_DESCRIPTOR_BYTES, read_json_object
from .models import BASE_PLUGIN, MAX_DEPLOYMENT_PLUGINS, PLUGIN_NAME, PluginBuild, ProjectInfo
from .verification import (
    configure_precompiled_module_rules,
    ignored_binary_items,
    read_plugin_module_names,
    verify_binary_plugin,
)


MAX_PROJECT_PLUGIN_REFERENCES = 4096


def _plugin_destination(parent: Path, root: Path, plugin_name: str, label: str) -> Path:
    if parent.exists() and is_reparse_point(parent):
        raise DeploymentError(f"refusing to install through a reparse-point {label}: {parent}")
    destination = parent / plugin_name
    if destination.exists() and is_reparse_point(destination):
        raise DeploymentError(f"refusing to replace a reparse-point plugin directory: {destination}")
    if not is_within(resolved(parent) / plugin_name, resolved(root)):
        raise DeploymentError(f"plugin destination escapes the selected {label}")
    return destination


def plugin_destination(project: ProjectInfo, plugin_name: str = PLUGIN_NAME) -> Path:
    return _plugin_destination(project.folder / "Plugins", project.folder, plugin_name, "project Plugins directory")


def engine_plugin_destination(engine_root: Path, plugin_name: str = PLUGIN_NAME) -> Path:
    engine_root = resolved(engine_root)
    plugins = engine_root / "Engine" / "Plugins"
    if plugins.exists() and is_reparse_point(plugins):
        raise DeploymentError(f"refusing to install through a reparse-point Engine Plugins directory: {plugins}")
    return _plugin_destination(plugins / "Marketplace", engine_root, plugin_name, "Engine Plugins directory")


def project_descriptor_update(project: ProjectInfo, plugin_names: Sequence[str]) -> tuple[bytes, bytes]:
    try:
        with project.descriptor.open("rb") as stream:
            original = stream.read(MAX_PROJECT_DESCRIPTOR_BYTES + 1)
        if len(original) > MAX_PROJECT_DESCRIPTOR_BYTES:
            raise DeploymentError(f"project descriptor is larger than 1 MiB: {project.descriptor}")
        value = json.loads(original.decode("utf-8-sig"))
    except DeploymentError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise DeploymentError(f"project descriptor is not readable JSON: {project.descriptor}: {error}") from error
    if not isinstance(value, dict):
        raise DeploymentError(f"project descriptor must contain one JSON object: {project.descriptor}")
    references = value.get("Plugins")
    if references is None:
        references = []
        value["Plugins"] = references
    if not isinstance(references, list):
        raise DeploymentError("project descriptor Plugins must be an array")
    if len(references) > MAX_PROJECT_PLUGIN_REFERENCES:
        raise DeploymentError(f"project descriptor contains more than {MAX_PROJECT_PLUGIN_REFERENCES} plugin references")
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
        raise DeploymentError(f"enabled project descriptor would contain more than {MAX_PROJECT_PLUGIN_REFERENCES} plugin references")
    encoded = (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    if len(encoded) > MAX_PROJECT_DESCRIPTOR_BYTES:
        raise DeploymentError("enabled project descriptor would be larger than 1 MiB")
    return original, encoded


def configured_project_descriptor(project: ProjectInfo, plugin_names: Sequence[str]) -> bytes:
    return project_descriptor_update(project, plugin_names)[1]


def write_project_descriptor(project: ProjectInfo, encoded: bytes, *, expected_original: bytes | None = None) -> None:
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
            raise DeploymentError(f"project descriptor changed while plugins were building; refusing to overwrite it: {descriptor}")
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


def configure_installed_descriptor(plugin_root: Path, plugin_name: str, enabled_by_default: bool | None) -> None:
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
    if not packages or len(packages) > MAX_DEPLOYMENT_PLUGINS:
        raise DeploymentError(f"deployment must contain one to {MAX_DEPLOYMENT_PLUGINS} plugins")
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
            module_names = read_plugin_module_names(package_root, plugin.name)
            shutil.copytree(
                package_root,
                staging,
                ignore=lambda directory, items, name=plugin.name, modules=module_names: ignored_binary_items(
                    directory,
                    items,
                    include_pdb=include_pdb,
                    plugin_name=name,
                    module_names=modules,
                ),
            )
            configure_precompiled_module_rules(staging, plugin.name)
            configure_installed_descriptor(staging, plugin.name, enabled_by_default)
            verify_binary_plugin(staging, include_pdb=include_pdb, plugin_name=plugin.name, enabled_by_default=enabled_by_default)
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
            verify_binary_plugin(destination, include_pdb=include_pdb, plugin_name=plugin.name, enabled_by_default=enabled_by_default)
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


def install_binary_plugin(package_root: Path, project: ProjectInfo, *, replace_existing: bool, include_pdb: bool = False) -> Path:
    return install_binary_plugins(
        ((BASE_PLUGIN, package_root, plugin_destination(project)),),
        replace_existing=replace_existing,
        include_pdb=include_pdb,
    )[0]
