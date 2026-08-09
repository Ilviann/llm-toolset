#!/usr/bin/env python3
"""Build and package the UnrealMCP plugin for binary deployment."""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import shlex
import subprocess
import sys
from pathlib import Path
from typing import Sequence


APPLICATION_ROOT = Path(__file__).resolve().parents[1]
WORKSPACE_ROOT = APPLICATION_ROOT.parent
PLUGIN_DESCRIPTOR = APPLICATION_ROOT / "plugin" / "UnrealMCP" / "UnrealMCP.uplugin"
FIXTURE_DESCRIPTOR = (
    APPLICATION_ROOT / "plugin" / "UnrealMCPTestCompanion" / "UnrealMCPTestCompanion.uplugin"
)
GAS_DESCRIPTOR = APPLICATION_ROOT / "plugin" / "UnrealMCPGAS" / "UnrealMCPGAS.uplugin"
COMMONUI_DESCRIPTOR = (
    APPLICATION_ROOT / "plugin" / "UnrealMCPCommonUI" / "UnrealMCPCommonUI.uplugin"
)
DEFAULT_OUTPUT = WORKSPACE_ROOT / "build" / "unreal-editor-mcp"
DEFAULT_FIXTURE_OUTPUT = WORKSPACE_ROOT / "build" / "unreal-mcp-test-companion"
DEFAULT_GAS_OUTPUT = WORKSPACE_ROOT / "build" / "unreal-mcp-gas"
DEFAULT_COMMONUI_OUTPUT = WORKSPACE_ROOT / "build" / "unreal-mcp-commonui"
ENGINE_ROOT_ENV = "UE58"
MAX_PLUGIN_DESCRIPTOR_BYTES = 1024 * 1024
PACKAGING_OWNED_DESCRIPTOR_FIELDS = frozenset({"EngineVersion", "Installed"})
_PLATFORM_NAME = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")


class PackagingError(RuntimeError):
    """Raised when packaging inputs or output do not satisfy the local contract."""


def resolved(path: Path) -> Path:
    return path.expanduser().resolve()


def is_within(path: Path, directory: Path) -> bool:
    try:
        path.relative_to(directory)
    except ValueError:
        return False
    return True


def validate_engine_root(engine_root: Path, host_system: str) -> Path:
    engine_root = resolved(engine_root)
    if not (engine_root / "Engine").is_dir():
        raise PackagingError(f"engine root must contain an Engine directory: {engine_root}")

    batch_files = engine_root / "Engine" / "Build" / "BatchFiles"
    run_uat = batch_files / ("RunUAT.bat" if host_system == "Windows" else "RunUAT.sh")
    if not run_uat.is_file():
        raise PackagingError(f"Unreal AutomationTool launcher does not exist: {run_uat}")
    if host_system != "Windows" and not os.access(run_uat, os.X_OK):
        raise PackagingError(f"Unreal AutomationTool launcher is not executable: {run_uat}")
    return run_uat


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
        size = path.stat().st_size
    except OSError as error:
        raise PackagingError(f"plugin descriptor is unavailable: {path}") from error
    if size > MAX_PLUGIN_DESCRIPTOR_BYTES:
        raise PackagingError(f"plugin descriptor is larger than 1 MiB: {path}")
    try:
        value = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise PackagingError(f"plugin descriptor is unreadable JSON: {path}: {error}") from error
    if not isinstance(value, dict):
        raise PackagingError(f"plugin descriptor must contain one JSON object: {path}")
    return value


def restore_source_descriptor_contract(output: Path, source_descriptor: Path) -> None:
    """Restore fields UAT may omit while retaining package-owned output metadata."""
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


def configure_environment(
    host_system: str,
    developer_dir: Path | None,
) -> dict[str, str]:
    environment = os.environ.copy()
    if host_system != "Darwin":
        return environment

    configured = developer_dir
    if configured is None:
        value = os.environ.get("UNREAL_MCP_DEVELOPER_DIR") or os.environ.get("DEVELOPER_DIR")
        configured = Path(value) if value else None
    if configured is None:
        raise PackagingError(
            "UNREAL_MCP_DEVELOPER_DIR or --developer-dir is required for a reproducible macOS build"
        )
    configured = resolved(configured)
    if not (configured / "usr" / "bin" / "xcodebuild").is_file():
        raise PackagingError(f"developer directory does not contain usr/bin/xcodebuild: {configured}")
    environment["DEVELOPER_DIR"] = str(configured)
    return environment


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
            raise PackagingError(
                f"packaged plugin descriptor changed source-owned field: {field}"
            )

    binaries = output / "Binaries"
    if not binaries.is_dir() or not any(path.is_file() for path in binaries.rglob("*")):
        raise PackagingError(f"packaging completed without compiled binaries: {binaries}")


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Build UnrealMCP with Unreal AutomationTool for binary deployment.",
    )
    plugin_selection = parser.add_mutually_exclusive_group()
    plugin_selection.add_argument(
        "--companion-fixture",
        action="store_true",
        help="package the disposable UnrealMCPTestCompanion instead of the base plugin",
    )
    plugin_selection.add_argument(
        "--gas-companion",
        action="store_true",
        help="package the optional UnrealMCPGAS companion instead of the base plugin",
    )
    plugin_selection.add_argument(
        "--commonui-companion",
        action="store_true",
        help="package the optional UnrealMCPCommonUI companion instead of the base plugin",
    )
    parser.add_argument(
        "--engine-root",
        type=Path,
        help=f"Unreal Engine installation root; defaults to {ENGINE_ROOT_ENV}.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT,
        help=f"package destination (default: {DEFAULT_OUTPUT})",
    )
    parser.add_argument(
        "--target-platforms",
        metavar="PLATFORM[+PLATFORM...]",
        help="optional UAT target-platform filter, for example Mac or Win64+Linux",
    )
    parser.add_argument(
        "--developer-dir",
        type=Path,
        help="macOS Xcode Contents/Developer path; defaults to UNREAL_MCP_DEVELOPER_DIR.",
    )
    parser.add_argument(
        "--strict-includes",
        action="store_true",
        help="ask UAT to disable PCH and unity builds while checking includes",
    )
    parser.add_argument(
        "--unversioned",
        action="store_true",
        help="do not embed the current Unreal Engine version in the packaged descriptor",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="validate inputs and print the UAT command without running it",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = create_parser()
    arguments = parser.parse_args(argv)
    host_system = platform.system()

    try:
        configured_engine = arguments.engine_root
        if configured_engine is None:
            value = os.environ.get(ENGINE_ROOT_ENV)
            configured_engine = Path(value) if value else None
        if configured_engine is None:
            raise PackagingError(f"{ENGINE_ROOT_ENV} or --engine-root is required")

        engine_root = resolved(configured_engine)
        run_uat = validate_engine_root(engine_root, host_system)
        plugin_descriptor = (
            FIXTURE_DESCRIPTOR if arguments.companion_fixture
            else GAS_DESCRIPTOR if arguments.gas_companion
            else COMMONUI_DESCRIPTOR if arguments.commonui_companion
            else PLUGIN_DESCRIPTOR
        )
        if arguments.companion_fixture and arguments.output == DEFAULT_OUTPUT:
            arguments.output = DEFAULT_FIXTURE_OUTPUT
        if arguments.gas_companion and arguments.output == DEFAULT_OUTPUT:
            arguments.output = DEFAULT_GAS_OUTPUT
        if arguments.commonui_companion and arguments.output == DEFAULT_OUTPUT:
            arguments.output = DEFAULT_COMMONUI_OUTPUT
        output = validate_output(arguments.output, engine_root, plugin_descriptor)
        target_platforms = normalize_target_platforms(arguments.target_platforms)
        environment = configure_environment(host_system, arguments.developer_dir)
        command = build_command(
            run_uat,
            output,
            target_platforms,
            strict_includes=arguments.strict_includes,
            unversioned=arguments.unversioned,
            plugin_descriptor=plugin_descriptor,
            dependency_plugins=(PLUGIN_DESCRIPTOR,) if (
                arguments.companion_fixture or arguments.gas_companion
                or arguments.commonui_companion
            ) else (),
        )
    except PackagingError as error:
        parser.error(str(error))

    print(f"Plugin: {plugin_descriptor}")
    print(f"Output: {output}")
    print(f"Command: {display_command(command, host_system)}")
    if arguments.dry_run:
        return 0

    result = subprocess.run(command, cwd=WORKSPACE_ROOT, env=environment, check=False)
    if result.returncode != 0:
        return result.returncode
    try:
        restore_source_descriptor_contract(output, plugin_descriptor)
        verify_package(output, plugin_descriptor)
    except PackagingError as error:
        print(f"Packaging verification failed: {error}", file=sys.stderr)
        return 1
    print(f"Packaged UnrealMCP binary plugin: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
