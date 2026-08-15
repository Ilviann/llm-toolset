"""Reusable packaging validation, command, execution, and verification services."""

from __future__ import annotations

import json
import re
import shlex
import subprocess
from pathlib import Path
from typing import Callable, Sequence

try:
    from scripts.unreal_tooling.engines import (
        ENGINE_ROOT_ENV,
        MAX_ENGINE_VERSION_BYTES,
        MIN_UNREAL_ENGINE_VERSION,
        configure_build_environment,
        read_engine_version,
        validate_engine_installation,
    )
    from scripts.unreal_tooling.errors import ToolingError
    from scripts.unreal_tooling.paths import is_within, resolved
    from scripts.unreal_tooling.plugins import (
        APPLICATION_ROOT,
        BASE_PLUGIN,
        COMMONUI_PLUGIN,
        ENHANCED_INPUT_PLUGIN,
        FIXTURE_PLUGIN,
        GAS_PLUGIN,
        WORKSPACE_ROOT,
    )
except ModuleNotFoundError:  # Direct script execution exposes scripts/ as the import root.
    from unreal_tooling.engines import (  # type: ignore[no-redef]
        ENGINE_ROOT_ENV,
        MAX_ENGINE_VERSION_BYTES,
        MIN_UNREAL_ENGINE_VERSION,
        configure_build_environment,
        read_engine_version,
        validate_engine_installation,
    )
    from unreal_tooling.errors import ToolingError  # type: ignore[no-redef]
    from unreal_tooling.paths import is_within, resolved  # type: ignore[no-redef]
    from unreal_tooling.plugins import (  # type: ignore[no-redef]
        APPLICATION_ROOT,
        BASE_PLUGIN,
        COMMONUI_PLUGIN,
        ENHANCED_INPUT_PLUGIN,
        FIXTURE_PLUGIN,
        GAS_PLUGIN,
        WORKSPACE_ROOT,
    )

from .models import PackageRequest, PackageResult, PreparedPackage


PackagingError = ToolingError
PLUGIN_DESCRIPTOR = BASE_PLUGIN.descriptor
FIXTURE_DESCRIPTOR = FIXTURE_PLUGIN.descriptor
GAS_DESCRIPTOR = GAS_PLUGIN.descriptor
COMMONUI_DESCRIPTOR = COMMONUI_PLUGIN.descriptor
ENHANCED_INPUT_DESCRIPTOR = ENHANCED_INPUT_PLUGIN.descriptor
DEFAULT_OUTPUT = WORKSPACE_ROOT / "build" / "unreal-editor-mcp"
DEFAULT_FIXTURE_OUTPUT = WORKSPACE_ROOT / "build" / "unreal-mcp-test-companion"
DEFAULT_GAS_OUTPUT = WORKSPACE_ROOT / "build" / "unreal-mcp-gas"
DEFAULT_COMMONUI_OUTPUT = WORKSPACE_ROOT / "build" / "unreal-mcp-commonui"
DEFAULT_ENHANCED_INPUT_OUTPUT = WORKSPACE_ROOT / "build" / "unreal-mcp-enhanced-input"
MAX_PLUGIN_DESCRIPTOR_BYTES = 1024 * 1024
PACKAGING_OWNED_DESCRIPTOR_FIELDS = frozenset({"EngineVersion", "Installed"})
_PLATFORM_NAME = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")


def validate_engine_root(engine_root: Path, host_system: str) -> Path:
    return validate_engine_installation(engine_root, host_system).run_uat


def validate_output(
    output: Path,
    engine_root: Path,
    plugin_descriptor: Path = PLUGIN_DESCRIPTOR,
) -> Path:
    output = resolved(output)
    workspace_root = resolved(WORKSPACE_ROOT)
    application_root = resolved(APPLICATION_ROOT)
    plugin_root = resolved(plugin_descriptor.parent)
    engine_root = resolved(engine_root)
    home = resolved(Path.home())
    if output.exists() and not output.is_dir():
        raise PackagingError(f"output path exists and is not a directory: {output}")
    if output == Path(output.anchor) or output in {
        home,
        workspace_root,
        application_root,
        plugin_root,
        engine_root,
        engine_root / "Engine",
    }:
        raise PackagingError(f"refusing to use a protected directory as packaging output: {output}")
    if is_within(workspace_root, output):
        raise PackagingError(f"output must not contain the workspace root: {output}")
    if is_within(output, plugin_root) or is_within(plugin_root, output):
        raise PackagingError(f"output must not overlap the source plugin directory: {output}")
    if is_within(output, engine_root) or is_within(engine_root, output):
        raise PackagingError(f"output must not overlap the Unreal Engine installation: {output}")
    return output


def normalize_target_platforms(value: str | None) -> str | None:
    if value is None:
        return None
    names = value.split("+")
    if not names or any(not _PLATFORM_NAME.fullmatch(name) for name in names):
        raise PackagingError("target platforms must be '+'-separated Unreal platform names")
    if len(set(names)) != len(names):
        raise PackagingError("target platform names must not be repeated")
    return "+".join(names)


def read_plugin_descriptor(path: Path) -> dict[str, object]:
    try:
        with path.open("rb") as stream:
            data = stream.read(MAX_PLUGIN_DESCRIPTOR_BYTES + 1)
        if len(data) > MAX_PLUGIN_DESCRIPTOR_BYTES:
            raise PackagingError(f"plugin descriptor is larger than 1 MiB: {path}")
        value = json.loads(data.decode("utf-8-sig"))
    except PackagingError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise PackagingError(f"plugin descriptor is unreadable JSON: {path}: {error}") from error
    if not isinstance(value, dict):
        raise PackagingError(f"plugin descriptor must contain one JSON object: {path}")
    return value


def restore_source_descriptor_contract(output: Path, source_descriptor: Path) -> None:
    packaged_path = output / source_descriptor.name
    packaged = read_plugin_descriptor(packaged_path)
    source = read_plugin_descriptor(source_descriptor)
    for field, value in source.items():
        if field not in PACKAGING_OWNED_DESCRIPTOR_FIELDS:
            packaged[field] = value
    try:
        packaged_path.write_text(
            json.dumps(packaged, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )
    except OSError as error:
        raise PackagingError(
            f"could not restore packaged plugin descriptor: {packaged_path}: {error}"
        ) from error


def build_command(
    run_uat: Path,
    output: Path,
    target_platforms: str | None,
    *,
    strict_includes: bool,
    unversioned: bool,
    plugin_descriptor: Path = PLUGIN_DESCRIPTOR,
    dependency_plugins: Sequence[Path] = (),
) -> list[str]:
    command = [
        str(run_uat),
        "BuildPlugin",
        f"-Plugin={plugin_descriptor}",
        f"-Package={output}",
        "-Rocket",
        "-NoP4",
        "-UTF8Output",
    ]
    command.extend(f"-Dependencies={dependency}" for dependency in dependency_plugins)
    if target_platforms is not None:
        command.append(f"-TargetPlatforms={target_platforms}")
    if strict_includes:
        command.append("-StrictIncludes")
    if unversioned:
        command.append("-Unversioned")
    return command


def display_command(command: Sequence[str], host_system: str) -> str:
    if host_system == "Windows":
        return subprocess.list2cmdline(command)
    return shlex.join(command)


def configure_environment(host_system: str, developer_dir: Path | None) -> dict[str, str]:
    return configure_build_environment(host_system, developer_dir)


def verify_package(output: Path, plugin_descriptor: Path = PLUGIN_DESCRIPTOR) -> None:
    descriptor_path = output / plugin_descriptor.name
    if not descriptor_path.is_file():
        raise PackagingError(f"packaging completed without the plugin descriptor: {descriptor_path}")
    descriptor = read_plugin_descriptor(descriptor_path)
    if descriptor.get("Installed") is not True:
        raise PackagingError("packaged plugin descriptor is not marked Installed")
    source = read_plugin_descriptor(plugin_descriptor)
    for field, value in source.items():
        if field not in PACKAGING_OWNED_DESCRIPTOR_FIELDS and (
            field not in descriptor or descriptor[field] != value
        ):
            raise PackagingError(f"packaged plugin descriptor changed source-owned field: {field}")
    binaries = output / "Binaries"
    if not binaries.is_dir() or not any(path.is_file() for path in binaries.rglob("*")):
        raise PackagingError(f"packaging completed without compiled binaries: {binaries}")


def prepare_package(request: PackageRequest) -> PreparedPackage:
    run_uat = validate_engine_root(request.engine_root, request.host_system)
    output = validate_output(request.output, request.engine_root, request.plugin_descriptor)
    target_platforms = normalize_target_platforms(request.target_platforms)
    environment = configure_environment(request.host_system, request.developer_dir)
    command = build_command(
        run_uat,
        output,
        target_platforms,
        strict_includes=request.strict_includes,
        unversioned=request.unversioned,
        plugin_descriptor=request.plugin_descriptor,
        dependency_plugins=request.dependency_plugins,
    )
    return PreparedPackage(request, run_uat, output, tuple(command), environment)


def execute_package(
    prepared: PreparedPackage,
    *,
    runner: Callable[..., subprocess.CompletedProcess[object]] = subprocess.run,
) -> PackageResult:
    result = runner(
        list(prepared.command),
        cwd=WORKSPACE_ROOT,
        env=dict(prepared.environment),
        check=False,
    )
    if result.returncode == 0:
        restore_source_descriptor_contract(prepared.output, prepared.request.plugin_descriptor)
        verify_package(prepared.output, prepared.request.plugin_descriptor)
    return PackageResult(prepared.output, prepared.command, result.returncode)
