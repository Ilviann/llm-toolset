"""Command-line adapter for the reusable packaging service."""

from __future__ import annotations

import argparse
import os
import platform
import sys
from pathlib import Path
from typing import Sequence

from .models import PackageRequest
from .service import (
    AI_DESCRIPTOR,
    COMMONUI_DESCRIPTOR,
    DEFAULT_COMMONUI_OUTPUT,
    DEFAULT_ENHANCED_INPUT_OUTPUT,
    DEFAULT_AI_OUTPUT,
    DEFAULT_FIXTURE_OUTPUT,
    DEFAULT_GAS_OUTPUT,
    DEFAULT_OUTPUT,
    ENGINE_ROOT_ENV,
    FIXTURE_DESCRIPTOR,
    GAS_DESCRIPTOR,
    ENHANCED_INPUT_DESCRIPTOR,
    PLUGIN_DESCRIPTOR,
    PackagingError,
    XCODE_APP_ENV,
    display_command,
    execute_package,
    prepare_package,
)


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Build UnrealMCP with Unreal AutomationTool for binary deployment."
    )
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--companion-fixture", action="store_true", help="package the disposable UnrealMCPTestCompanion instead of the base plugin")
    selection.add_argument("--gas-companion", action="store_true", help="package the optional UnrealMCPGAS companion instead of the base plugin")
    selection.add_argument("--commonui-companion", action="store_true", help="package the optional UnrealMCPCommonUI companion instead of the base plugin")
    selection.add_argument("--enhanced-input-companion", action="store_true", help="package the optional UnrealMCPEnhancedInput companion instead of the base plugin")
    selection.add_argument("--ai-companion", action="store_true", help="package the optional UnrealMCPAI companion instead of the base plugin")
    parser.add_argument("--engine-root", type=Path, help=f"Unreal Engine installation root; defaults to {ENGINE_ROOT_ENV}.")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT, help=f"package destination (default: {DEFAULT_OUTPUT})")
    parser.add_argument("--target-platforms", metavar="PLATFORM[+PLATFORM...]", help="optional UAT target-platform filter, for example Mac or Win64+Linux")
    parser.add_argument("--developer-dir", type=Path, help=f"macOS Xcode Contents/Developer path; defaults to {XCODE_APP_ENV}/Contents/Developer.")
    parser.add_argument("--strict-includes", action="store_true", help="ask UAT to disable PCH and unity builds while checking includes")
    parser.add_argument("--unversioned", action="store_true", help="do not embed the current Unreal Engine version in the packaged descriptor")
    parser.add_argument("--dry-run", action="store_true", help="validate inputs and print the UAT command without running it")
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
        descriptor = (
            FIXTURE_DESCRIPTOR if arguments.companion_fixture
            else GAS_DESCRIPTOR if arguments.gas_companion
            else COMMONUI_DESCRIPTOR if arguments.commonui_companion
            else ENHANCED_INPUT_DESCRIPTOR if arguments.enhanced_input_companion
            else AI_DESCRIPTOR if arguments.ai_companion
            else PLUGIN_DESCRIPTOR
        )
        output = arguments.output
        if output == DEFAULT_OUTPUT:
            if arguments.companion_fixture:
                output = DEFAULT_FIXTURE_OUTPUT
            elif arguments.gas_companion:
                output = DEFAULT_GAS_OUTPUT
            elif arguments.commonui_companion:
                output = DEFAULT_COMMONUI_OUTPUT
            elif arguments.enhanced_input_companion:
                output = DEFAULT_ENHANCED_INPUT_OUTPUT
            elif arguments.ai_companion:
                output = DEFAULT_AI_OUTPUT
        is_companion = (
            arguments.companion_fixture or arguments.gas_companion
            or arguments.commonui_companion or arguments.enhanced_input_companion
            or arguments.ai_companion
        )
        prepared = prepare_package(PackageRequest(
            engine_root=configured_engine,
            output=output,
            plugin_descriptor=descriptor,
            dependency_plugins=(PLUGIN_DESCRIPTOR,) if is_companion else (),
            target_platforms=arguments.target_platforms,
            strict_includes=arguments.strict_includes,
            unversioned=arguments.unversioned,
            developer_dir=arguments.developer_dir,
            host_system=host_system,
        ))
    except PackagingError as error:
        parser.error(str(error))
    print(f"Plugin: {prepared.request.plugin_descriptor}")
    print(f"Output: {prepared.output}")
    print(f"Command: {display_command(prepared.command, host_system)}")
    if arguments.dry_run:
        return 0
    try:
        result = execute_package(prepared)
    except PackagingError as error:
        print(f"Packaging verification failed: {error}", file=sys.stderr)
        return 1
    if result.return_code != 0:
        return result.return_code
    print(f"Packaged UnrealMCP binary plugin: {result.output}")
    return 0
