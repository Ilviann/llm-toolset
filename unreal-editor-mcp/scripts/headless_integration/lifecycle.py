#!/usr/bin/env python3
"""Coordinate decomposed disposable-editor cross-process scenarios."""

from __future__ import annotations

import platform
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from unreal_editor_mcp.bridge import UnrealBridge  # noqa: E402
from unreal_editor_mcp.discovery import read_discovery  # noqa: E402
from unreal_editor_mcp.errors import BridgeError, ErrorCode  # noqa: E402
from unreal_editor_mcp.project import ProjectLayout  # noqa: E402

from .assets import run_asset_scenario, verify_restarted_assets
from .automation import prepare_commonui_widget_fixture, prepare_gas_effect_fixture, prepare_phase_two_fixture, run_automation
from .blueprints import author_blueprint_scenario, prepare_blueprint_scenario, verify_restarted_blueprints
from .capability_contract import verify_capability_contract
from .companions import verify_companion_scenario
from .game_data import author_phase_seventeen_game_data, verify_restarted_game_data_and_level
from .level_editing import author_level_edit_scenario, verify_restarted_level_edit
from .level_management import manage_disposable_level, verify_restarted_level_deletion
from .level_opening import open_acceptance_level
from .operations import reconcile_operation, send_without_reading
from .process_lifecycle import (
    EditorProcessConfig,
    configure_editor_environment,
    launch_editor,
    reject_bad_token,
    required_path,
    resolve_editor_executable,
    resolve_lifecycle_editor_executable,
    shutdown_editor,
    stop_editor,
    verify_loopback_only,
    wait_until_ready,
)
from .readonly_mode import verify_readonly_mode, verify_windows_readonly_lifecycle
from .widgets import author_widget_scenario, verify_restarted_widgets

ENGINE_ROOT_ENV = "UE58"
TEST_PROJECT_ENV = "UNREAL_MCP_TEST_UPROJECT"

GAMEPLAY_TAG_FIXTURE = """[/Script/GameplayTags.GameplayTagsList]
GameplayTagList=(Tag="UnrealMCP.Test",DevComment="Unreal MCP automation fixture")
GameplayTagList=(Tag="UnrealMCP.Test.Child",DevComment="Unreal MCP automation fixture")
"""


def prepare_gameplay_tag_fixture(layout: ProjectLayout) -> None:
    """Install deterministic project-defined tags only in the disposable test project."""
    fixture = layout.root / "Config" / "Tags" / "UnrealMCPTests.ini"
    fixture.parent.mkdir(parents=True, exist_ok=True)
    fixture.write_text(GAMEPLAY_TAG_FIXTURE, encoding="utf-8", newline="\n")

def restore_framework_defaults(
    bridge: UnrealBridge,
    project_hash: str,
) -> None:
    defaults = {
        "default_game_mode": "/Script/Engine.GameModeBase",
        "default_game_instance": "/Script/Engine.GameInstance",
    }
    for setting, default_class in defaults.items():
        arguments = {
            "operation_id": uuid.uuid4().hex,
            "project_hash": project_hash,
            "setting": setting,
            "class_path": default_class,
            "expected_class": default_class,
        }
        try:
            bridge.call("gameplay_framework_edit", arguments)
        except BridgeError as error:
            if error.code == ErrorCode.INVALID_ARGUMENT:
                continue
            if error.code != ErrorCode.STALE_PRECONDITION:
                raise
            current_class = error.details.get("current_class")
            if not isinstance(current_class, str) or not current_class:
                raise
            arguments["operation_id"] = uuid.uuid4().hex
            arguments["expected_class"] = current_class
            result = bridge.call("gameplay_framework_edit", arguments)
            if result.get("verified") is not True or result.get("new_class") != default_class:
                raise AssertionError(f"gameplay-framework retry cleanup failed: {result!r}")



def run_widget_restart_integration(
    executable: Path,
    layout: ProjectLayout,
    environment: dict[str, str],
) -> int:
    """Run the focused Widget Blueprint author/save/restart inspection gate."""
    command = [
        str(executable), str(layout.descriptor), "-unattended", "-nop4", "-nosplash",
        "-nullrhi", "-nosound", "-nocrashreports", "-NoAssetRegistryCache",
        "-DDC-ForceMemoryCache",
    ]
    process_config = EditorProcessConfig(
        executable,
        layout.descriptor,
        tuple(command[2:]),
        ROOT,
        environment,
    )
    with tempfile.TemporaryFile() as log:
        bridge = None
        process = launch_editor(process_config, log)
        try:
            wait_until_ready(layout, process, time.monotonic() + 120.0)
            bridge = UnrealBridge(layout, timeout=3.0)
            scenario = author_widget_scenario(bridge)
            shutdown_editor(bridge, process)
        except Exception:
            log.seek(0)
            sys.stderr.buffer.write(log.read()[-32_000:])
            raise
        finally:
            stop_editor(process, bridge=bridge)

        bridge = None
        process = launch_editor(process_config, log)
        try:
            wait_until_ready(layout, process, time.monotonic() + 120.0)
            bridge = UnrealBridge(layout, timeout=3.0)
            verify_restarted_widgets(bridge, scenario)
            shutdown_editor(bridge, process)
        except Exception:
            log.seek(0)
            sys.stderr.buffer.write(log.read()[-32_000:])
            raise
        finally:
            stop_editor(process, bridge=bridge)
    print("Integration passed: Widget Blueprint authoring, save, restart, and inspection")
    return 0



def main() -> int:
    engine = required_path(ENGINE_ROOT_ENV)
    project = required_path(TEST_PROJECT_ENV)
    host_system = platform.system()
    executable = resolve_editor_executable(engine, host_system)
    environment = configure_editor_environment(host_system)
    layout = ProjectLayout.resolve(project)
    prepare_gameplay_tag_fixture(layout)
    if sys.argv[1:] == ["--readonly-lifecycle-only"]:
        lifecycle_executable = resolve_lifecycle_editor_executable(engine, host_system)
        verify_windows_readonly_lifecycle(layout, lifecycle_executable)
        print(
            "Acceptance passed: readonly+lifecycle catalog, access rejection, "
            "real launch/restart/shutdown, bridge replacement, and content preservation"
        )
        return 0
    phase_two_fixture = layout.root / "Content" / "UnrealMCPPhase2" / "BP_InspectionFixture.uasset"
    phase_two_fixture.unlink(missing_ok=True)
    phase_four_fixture = layout.root / "Content" / "UnrealMCPPhase4" / "BP_ComponentFixture.uasset"
    phase_four_fixture.unlink(missing_ok=True)
    phase_fourteen_dir = layout.root / "Content" / "UnrealMCPPhase14"
    for name in ("BP_GameModeBase", "BP_GameMode", "BP_GameStateBase", "BP_GameState"):
        (phase_fourteen_dir / f"{name}.uasset").unlink(missing_ok=True)
    phase_fifteen_fixture = layout.root / "Content" / "UnrealMCPPhase15" / "BP_GameInstance.uasset"
    phase_fifteen_fixture.unlink(missing_ok=True)
    phase_seventeen_dir = layout.root / "Content" / "UnrealMCPPhase17"
    for name in ("ST_WeaponStats", "DT_WeaponStats", "DT_GameplayTags"):
        (phase_seventeen_dir / f"{name}.uasset").unlink(missing_ok=True)
    (layout.root / "Content" / "UnrealMCPAssetDelete" / "DT_Disposable.uasset").unlink(
        missing_ok=True
    )
    (layout.root / "Content" / "UnrealMCPWidgetTree" / "WBP_WidgetTree.uasset").unlink(
        missing_ok=True
    )
    gas_fixture_dir = layout.root / "Content" / "UnrealMCPGAS"
    for name in (
        "GE_InspectionFixture", "GA_EffectReferenceFixture", "GCN_StaticFixture",
        "GCN_ActorFixture", "AS_InspectionFixture", "MMC_InspectionFixture",
        "Exec_InspectionFixture",
    ):
        (gas_fixture_dir / f"{name}.uasset").unlink(missing_ok=True)
    commonui_fixture = (
        layout.root / "Content" / "UnrealMCPCommonUI" / "WBP_InspectionFixture.uasset"
    )
    commonui_fixture.unlink(missing_ok=True)
    if sys.argv[1:] == ["--automation-only"]:
        return run_automation(executable, layout.descriptor, environment)
    if len(sys.argv) == 3 and sys.argv[1] == "--automation-filter":
        return run_automation(executable, layout.descriptor, environment, sys.argv[2])
    if sys.argv[1:] == ["--widget-only"]:
        return run_widget_restart_integration(executable, layout, environment)
    if sys.argv[1:]:
        raise SystemExit(
            "usage: run_headless_integration.py "
            "[--automation-only | --automation-filter PREFIX | --widget-only | "
            "--readonly-lifecycle-only]"
        )
    saved_fixture_snapshot = prepare_phase_two_fixture(executable, layout.descriptor, environment)
    if len(saved_fixture_snapshot) != 40:
        raise AssertionError("Phase 2 saved fixture did not report a structural snapshot")
    gas_effect_fixture = prepare_gas_effect_fixture(executable, layout.descriptor, environment)
    if gas_effect_fixture != "/Game/UnrealMCPGAS/GE_InspectionFixture.GE_InspectionFixture":
        raise AssertionError(f"Gameplay Effect fixture path mismatch: {gas_effect_fixture!r}")
    commonui_widget_fixture = prepare_commonui_widget_fixture(
        executable, layout.descriptor, environment,
    )
    if commonui_widget_fixture \
            != "/Game/UnrealMCPCommonUI/WBP_InspectionFixture.WBP_InspectionFixture":
        raise AssertionError(f"CommonUI Widget fixture path mismatch: {commonui_widget_fixture!r}")
    command = [
        str(executable), str(layout.descriptor), "-unattended", "-nop4", "-nosplash",
        "-nullrhi", "-nosound", "-nocrashreports", "-NoAssetRegistryCache",
        "-DDC-ForceMemoryCache",
    ]
    process_config = EditorProcessConfig(
        executable,
        layout.descriptor,
        tuple(command[2:]),
        ROOT,
        environment,
    )
    with tempfile.TemporaryFile() as log:
        bridge = None
        process = launch_editor(process_config, log)
        try:
            wait_until_ready(layout, process, time.monotonic() + 120.0)
            bridge = UnrealBridge(layout, timeout=3.0)
            capabilities = bridge.call("capabilities")
            restore_framework_defaults(bridge, capabilities["project_hash"])
            state = bridge.call("editor_state")
            verify_capability_contract(capabilities, state)
            verify_companion_scenario(bridge, capabilities)
            level_scenario = open_acceptance_level(
                bridge,
                layout,
                capabilities["bridge_instance_id"],
            )
            deleted_level_path = manage_disposable_level(
                bridge,
                layout,
                capabilities["bridge_instance_id"],
            )
            blueprint_fixtures = prepare_blueprint_scenario(bridge)
            phase_two_loaded_snapshot = blueprint_fixtures["phase_two_loaded_snapshot"]
            phase_two_loaded_inspection = blueprint_fixtures["phase_two_loaded_inspection"]
            created = blueprint_fixtures["created"]
            phase_fourteen_families = blueprint_fixtures["phase_fourteen_families"]
            phase_fifteen_game_instance = blueprint_fixtures["phase_fifteen_game_instance"]
            level_edit_scenario = author_level_edit_scenario(
                bridge,
                layout,
                capabilities["bridge_instance_id"],
            )
            phase_seventeen_game_data = author_phase_seventeen_game_data(
                bridge, layout, capabilities["bridge_instance_id"],
            )
            asset_scenario = run_asset_scenario(
                bridge,
                layout,
                capabilities["bridge_instance_id"],
                phase_seventeen_game_data,
            )
            disposable_path = asset_scenario["disposable_path"]
            blueprint_scenario = author_blueprint_scenario(
                bridge,
                layout,
                capabilities,
                created,
                phase_fourteen_families,
                phase_fifteen_game_instance,
            )
            widget_scenario = author_widget_scenario(bridge)
            verify_readonly_mode(
                bridge,
                layout,
                bridge_instance_id=capabilities["bridge_instance_id"],
                blueprint_path="/Game/UnrealMCPPhase4/BP_ComponentFixture.BP_ComponentFixture",
                game_data_path=phase_seventeen_game_data["table_path"],
                map_path=level_scenario["map_path"],
            )
            assigned_game_instance_class = blueprint_scenario["assigned_game_instance_class"]
            assigned_game_mode_class = blueprint_scenario["assigned_game_mode_class"]
            reject_bad_token(layout)
            verify_loopback_only(read_discovery(layout).port)
            shutdown_editor(bridge, process)
        except Exception:
            log.seek(0)
            sys.stderr.buffer.write(log.read()[-32_000:])
            raise
        finally:
            stop_editor(process, bridge=bridge)
        reloaded_bridge = None
        process = launch_editor(process_config, log)
        try:
            wait_until_ready(layout, process, time.monotonic() + 120.0)
            reloaded_bridge = UnrealBridge(layout, timeout=3.0)
            reloaded_capabilities = reloaded_bridge.call("capabilities")
            restored_game_mode = reloaded_bridge.call("gameplay_framework_edit", {
                "operation_id": uuid.uuid4().hex,
                "project_hash": reloaded_capabilities["project_hash"],
                "setting": "default_game_mode",
                "class_path": "/Script/Engine.GameModeBase",
                "expected_class": assigned_game_mode_class,
            })
            restored_game_instance = reloaded_bridge.call("gameplay_framework_edit", {
                "operation_id": uuid.uuid4().hex,
                "project_hash": reloaded_capabilities["project_hash"],
                "setting": "default_game_instance",
                "class_path": "/Script/Engine.GameInstance",
                "expected_class": assigned_game_instance_class,
            })
            if restored_game_mode.get("verified") is not True or restored_game_instance.get("verified") is not True:
                raise AssertionError("Phase 16 framework settings did not survive restart and restore")
            verify_restarted_widgets(reloaded_bridge, widget_scenario)
            verify_restarted_game_data_and_level(
                reloaded_bridge,
                phase_seventeen_game_data,
                level_scenario,
            )
            verify_restarted_level_edit(reloaded_bridge, level_edit_scenario)
            verify_restarted_level_deletion(reloaded_bridge, deleted_level_path)
            verify_restarted_assets(
                reloaded_bridge,
                phase_seventeen_game_data,
                disposable_path,
            )
            verify_restarted_blueprints(
                reloaded_bridge,
                layout,
                phase_two_loaded_snapshot,
                phase_two_loaded_inspection,
                phase_fourteen_families,
                phase_fifteen_game_instance,
                blueprint_scenario,
            )
            shutdown_editor(reloaded_bridge, process)
        except Exception:
            log.seek(0)
            sys.stderr.buffer.write(log.read()[-32_000:])
            raise
        finally:
            stop_editor(process, bridge=reloaded_bridge)
    deadline = time.monotonic() + 5.0
    while layout.discovery_file.exists() and time.monotonic() < deadline:
        time.sleep(0.1)
    if layout.discovery_file.exists():
        try:
            read_discovery(layout)
        except Exception:
            pass
        else:
            raise AssertionError("a live discovery heartbeat remained after editor termination")
    print("Integration passed: Widget Blueprint authoring, asset references/deletion, level management/editing, Phase 17 authoring, replay/restart, and graceful lifecycle shutdown")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
