#!/usr/bin/env python3
"""Launch the disposable editor and coordinate the cross-process scenarios."""

from __future__ import annotations

import http.client
import json
import os
import platform
import signal
import socket
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from unreal_editor_mcp import __version__  # noqa: E402
from unreal_editor_mcp.bridge import BRIDGE_PATH, UnrealBridge  # noqa: E402
from unreal_editor_mcp.discovery import read_discovery, read_token  # noqa: E402
from unreal_editor_mcp.errors import BridgeError, ErrorCode  # noqa: E402
from unreal_editor_mcp.project import ProjectLayout  # noqa: E402

from .assets import run_asset_scenario, verify_restarted_assets
from .blueprints import (
    author_blueprint_scenario,
    prepare_blueprint_scenario,
    verify_restarted_blueprints,
)
from .game_data_levels import (
    author_level_edit_scenario,
    author_phase_seventeen_game_data,
    manage_disposable_level,
    open_acceptance_level,
    verify_restarted_game_data_and_level,
    verify_restarted_level_edit,
    verify_restarted_level_deletion,
)
from .readonly_mode import verify_readonly_mode, verify_windows_readonly_lifecycle
from .widgets import author_widget_scenario, verify_restarted_widgets


def required_path(name: str) -> Path:
    value = os.environ.get(name)
    if not value:
        raise SystemExit(f"{name} is required")
    path = Path(value).expanduser().resolve()
    if not path.exists():
        raise SystemExit(f"{name} does not exist: {path}")
    return path


def resolve_editor_executable(engine: Path, host_system: str) -> Path:
    relative_paths = {
        "Darwin": Path("Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"),
        "Windows": Path("Engine/Binaries/Win64/UnrealEditor-Cmd.exe"),
        "Linux": Path("Engine/Binaries/Linux/UnrealEditor"),
    }
    relative = relative_paths.get(host_system)
    if relative is None:
        raise SystemExit(f"unsupported host platform: {host_system}")
    executable = engine / relative
    if not executable.is_file():
        raise SystemExit(f"Unreal Editor executable not found: {executable}")
    return executable


def resolve_lifecycle_editor_executable(engine: Path, host_system: str) -> Path:
    if host_system != "Windows":
        raise SystemExit("readonly lifecycle acceptance is required only on Windows")
    executable = engine / "Engine/Binaries/Win64/UnrealEditor.exe"
    if not executable.is_file():
        raise SystemExit(f"Unreal lifecycle executable not found: {executable}")
    return executable


def configure_editor_environment(host_system: str) -> dict[str, str]:
    environment = dict(os.environ)
    if host_system == "Darwin":
        environment["DEVELOPER_DIR"] = str(required_path("UNREAL_MCP_DEVELOPER_DIR"))
    return environment


def wait_until_ready(layout: ProjectLayout, process: subprocess.Popen[bytes], deadline: float) -> None:
    last_error = "discovery record not created"
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"Unreal Editor exited before bridge startup ({process.returncode})")
        try:
            record = read_discovery(layout)
            result = UnrealBridge(layout, timeout=2.0).call("capabilities")
            if result.get("bridge_ready") is True and record.bridge_version == __version__:
                return
        except Exception as error:
            last_error = str(error)
        time.sleep(0.25)
    raise TimeoutError(f"Unreal bridge did not become ready: {last_error}")


def reject_bad_token(layout: ProjectLayout) -> None:
    record = read_discovery(layout)
    connection = http.client.HTTPConnection("127.0.0.1", record.port, timeout=2.0)
    try:
        connection.request(
            "POST",
            BRIDGE_PATH,
            body=b'{"command":"capabilities","arguments":{}}',
            headers={"Authorization": "Bearer " + "0" * 64, "Content-Type": "application/json"},
        )
        response = connection.getresponse()
        payload = json.loads(response.read(4096))
    finally:
        connection.close()
    if response.status != 401 or payload.get("error", {}).get("code") != "authentication_failed":
        raise AssertionError(f"bad token was not rejected safely: HTTP {response.status} {payload!r}")


def verify_loopback_only(port: int) -> None:
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect(("192.0.2.1", 9))
        non_loopback = probe.getsockname()[0]
    finally:
        probe.close()
    if non_loopback.startswith("127."):
        raise RuntimeError("could not resolve a non-loopback local interface")
    try:
        peer = socket.create_connection((non_loopback, port), timeout=0.5)
    except OSError:
        return
    else:
        peer.close()
        raise AssertionError(f"bridge unexpectedly accepted connections on {non_loopback}:{port}")


def send_without_reading(layout: ProjectLayout, command: str, arguments: dict[str, object]) -> None:
    """Submit a mutation and deliberately discard its HTTP response."""
    record = read_discovery(layout)
    connection = http.client.HTTPConnection("127.0.0.1", record.port, timeout=2.0)
    connection.request(
        "POST",
        BRIDGE_PATH,
        body=json.dumps({"command": command, "arguments": arguments}, separators=(",", ":")).encode(),
        headers={
            "Authorization": "Bearer " + read_token(layout),
            "Content-Type": "application/json",
            "X-Unreal-MCP-Version": __version__,
        },
    )
    connection.close()


def reconcile_operation(bridge: UnrealBridge, operation_id: str, bridge_instance_id: str) -> dict[str, object]:
    deadline = time.monotonic() + 10.0
    while time.monotonic() < deadline:
        status = bridge.call("operation_status", {
            "operation_id": operation_id,
            "bridge_instance_id": bridge_instance_id,
        })
        if status.get("state") in {
            "committed", "partial", "rejected", "cancelled", "outcome_unknown",
        }:
            return status
        time.sleep(0.05)
    raise TimeoutError("lost mutation response did not reach a retained terminal state")


def stop_editor(
    process: subprocess.Popen[bytes],
    timeout: float = 30.0,
    bridge: UnrealBridge | None = None,
) -> None:
    if process.poll() is not None:
        return
    if bridge is not None:
        try:
            shutdown_editor(bridge, process, timeout)
            return
        except Exception:
            pass
    process.send_signal(signal.SIGTERM)
    try:
        process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=10)
        raise RuntimeError("Unreal Editor did not unload cleanly")


def shutdown_editor(bridge: UnrealBridge, process: subprocess.Popen[bytes], timeout: float = 30.0) -> None:
    try:
        shutdown = bridge.call("editor_shutdown")
    except BridgeError as error:
        raise AssertionError(f"graceful shutdown was refused: {error.as_dict()!r}") from error
    if shutdown.get("accepted") is not True:
        raise AssertionError(f"graceful shutdown was not accepted: {shutdown!r}")
    deadline = time.monotonic() + timeout
    while process.poll() is None and time.monotonic() < deadline:
        time.sleep(0.1)
    if process.poll() is None:
        raise AssertionError("graceful shutdown did not terminate the configured editor")
    if process.returncode != 0:
        raise AssertionError(f"graceful shutdown exited with status {process.returncode}")


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
    ]
    with tempfile.TemporaryFile() as log:
        bridge = None
        process = subprocess.Popen(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT)
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
        process = subprocess.Popen(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT)
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


def run_automation(executable: Path, project: Path, environment: dict[str, str], test_filter: str = "UnrealMCP") -> int:
    all_expected = (
        "CompatibilityBranch",
        "ErrorEnvelope",
        "GameThreadDispatch",
        "InvalidTokenFailsClosed",
        "ProtocolBounds",
        "RouteGuards",
        "TokenPersistence",
        "CursorGuards",
        "InspectionContracts",
        "LiveFixture",
        "CreationContracts",
        "FailureCleanup",
        "CreationLiveFixture",
        "ComponentAndDefaultEdits",
        "OperationLedger",
        "PropertyCodec",
        "K2TypeCodec",
        "MemberVariables",
        "FunctionsAndLocals",
        "MacrosAndCustomEvents",
        "ActionCatalog",
        "ExpandedActionCatalog",
        "GraphNodeLifecycle",
        "PinDefaultsAndDirectConnections",
        "WildcardsConversionsAndAtomicGraphEditing",
        "GameModeAndGameStateFamilies",
        "GameInstanceFamily",
        "MultiplayerAuthoring",
        "FrameworkAssignment",
        "GameDataAuthoring",
        "DiscoverySnapshotsAndSafety",
        "ActorsComponentsPropertiesAndSafety",
        "CreateConfigurePersistAndDelete",
        "PreflightPersistenceAndReferences",
        "RegistryLiveMemoryAndCursors",
        "FamilyInspectionMutationAndPersistence",
        "LayoutStyleBindingsAndEvents",
        "PreflightTransactionPreservation",
        "PreservationAcrossReadonlyFlows",
        "Protocol",
    )
    if test_filter == "UnrealMCP":
        expected = all_expected
    elif test_filter == "UnrealMCP.Phase4":
        expected = tuple(name for name in all_expected if name in {
            "ComponentAndDefaultEdits", "OperationLedger", "PropertyCodec",
        })
    elif test_filter == "UnrealMCP.Phase5":
        expected = tuple(name for name in all_expected if name in {"K2TypeCodec", "MemberVariables"})
    elif test_filter == "UnrealMCP.Phase6":
        expected = tuple(name for name in all_expected if name == "FunctionsAndLocals")
    elif test_filter == "UnrealMCP.Phase7":
        expected = tuple(name for name in all_expected if name == "MacrosAndCustomEvents")
    elif test_filter == "UnrealMCP.Phase8":
        expected = tuple(name for name in all_expected if name == "ActionCatalog")
    elif test_filter == "UnrealMCP.Phase10":
        expected = tuple(name for name in all_expected if name == "ExpandedActionCatalog")
    elif test_filter == "UnrealMCP.Phase11":
        expected = tuple(name for name in all_expected if name == "GraphNodeLifecycle")
    elif test_filter == "UnrealMCP.Phase12":
        expected = tuple(name for name in all_expected if name == "PinDefaultsAndDirectConnections")
    elif test_filter == "UnrealMCP.Phase13":
        expected = tuple(name for name in all_expected if name == "WildcardsConversionsAndAtomicGraphEditing")
    elif test_filter == "UnrealMCP.Phase14":
        expected = tuple(name for name in all_expected if name == "GameModeAndGameStateFamilies")
    elif test_filter == "UnrealMCP.Phase15":
        expected = tuple(name for name in all_expected if name == "GameInstanceFamily")
    elif test_filter == "UnrealMCP.Phase16":
        expected = tuple(name for name in all_expected if name in {"MultiplayerAuthoring", "FrameworkAssignment"})
    elif test_filter == "UnrealMCP.Phase17":
        expected = tuple(name for name in all_expected if name == "GameDataAuthoring")
    elif test_filter == "UnrealMCP.AssetReferences":
        expected = tuple(name for name in all_expected if name == "RegistryLiveMemoryAndCursors")
    elif test_filter == "UnrealMCP.LevelManagement":
        expected = tuple(name for name in all_expected if name == "CreateConfigurePersistAndDelete")
    else:
        leaf = test_filter.rsplit(".", 1)[-1]
        expected = (leaf,) if leaf in all_expected else ()
    command = [
        str(executable), str(project), "-unattended", "-nop4", "-nosplash", "-nullrhi",
        "-stdout", "-FullStdOutLogOutput", "-nocrashreports", "-NoAssetRegistryCache",
        f"-ExecCmds=Automation RunTests {test_filter};Quit",
        "-TestExit=Automation Test Queue Empty",
    ]
    with tempfile.TemporaryFile() as log:
        process = subprocess.Popen(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT)
        try:
            return_code = process.wait(timeout=180.0)
        except subprocess.TimeoutExpired:
            stop_editor(process)
            raise RuntimeError("Unreal Automation Tests exceeded the three-minute deadline")
        log.seek(0)
        output = log.read().decode("utf-8", errors="replace")
    missing = [name for name in expected if f"Result={{Success}} Name={{{name}}}" not in output]
    if return_code != 0 or "TEST COMPLETE. EXIT CODE: 0" not in output or missing:
        sys.stderr.write(output[-32_000:])
        if missing:
            raise RuntimeError(f"Unreal Automation Tests did not pass: {', '.join(missing)}")
        raise RuntimeError(f"Unreal Automation Tests exited with status {return_code}")
    print(f"Unreal Automation Tests passed: {len(expected)} native cases")
    return 0


def prepare_phase_two_fixture(executable: Path, project: Path, environment: dict[str, str]) -> str:
    command = [
        str(executable), str(project), "-unattended", "-nop4", "-nosplash", "-nullrhi",
        "-stdout", "-FullStdOutLogOutput", "-nocrashreports", "-NoAssetRegistryCache",
        "-ExecCmds=Automation RunTests UnrealMCP.Phase2.LiveFixture;Quit",
        "-TestExit=Automation Test Queue Empty",
    ]
    with tempfile.TemporaryFile() as log:
        process = subprocess.Popen(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT)
        try:
            return_code = process.wait(timeout=180.0)
        except subprocess.TimeoutExpired:
            stop_editor(process)
            raise RuntimeError("Phase 2 fixture preparation exceeded the three-minute deadline")
        log.seek(0)
        output = log.read().decode("utf-8", errors="replace")
    marker = "UNREAL_MCP_PHASE2_SNAPSHOT="
    snapshots = [line.split(marker, 1)[1].split()[0] for line in output.splitlines() if marker in line]
    if return_code != 0 or "Result={Success} Name={LiveFixture}" not in output or not snapshots:
        sys.stderr.write(output[-32_000:])
        raise RuntimeError("Phase 2 saved fixture preparation failed")
    return snapshots[-1]


def main() -> int:
    engine = required_path("UNREAL_MCP_ENGINE_ROOT")
    project = required_path("UNREAL_MCP_TEST_UPROJECT")
    host_system = platform.system()
    executable = resolve_editor_executable(engine, host_system)
    environment = configure_editor_environment(host_system)
    layout = ProjectLayout.resolve(project)
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
    for name in ("ST_WeaponStats", "DT_WeaponStats"):
        (phase_seventeen_dir / f"{name}.uasset").unlink(missing_ok=True)
    (layout.root / "Content" / "UnrealMCPAssetDelete" / "DT_Disposable.uasset").unlink(
        missing_ok=True
    )
    (layout.root / "Content" / "UnrealMCPWidgetTree" / "WBP_WidgetTree.uasset").unlink(
        missing_ok=True
    )
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
    command = [
        str(executable), str(layout.descriptor), "-unattended", "-nop4", "-nosplash",
        "-nullrhi", "-nosound", "-nocrashreports", "-NoAssetRegistryCache",
    ]
    with tempfile.TemporaryFile() as log:
        bridge = None
        process = subprocess.Popen(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT)
        try:
            wait_until_ready(layout, process, time.monotonic() + 120.0)
            bridge = UnrealBridge(layout, timeout=3.0)
            capabilities = bridge.call("capabilities")
            restore_framework_defaults(bridge, capabilities["project_hash"])
            state = bridge.call("editor_state")
            if capabilities.get("commands") != [
                "capabilities", "editor_state", "editor_shutdown", "operation_status", "operation_cancel",
                "asset_references", "asset_delete",
                "level_inspect", "level_open", "level_manage", "level_actor_edit", "level_save",
                "blueprint_inspect", "blueprint_action_catalog", "blueprint_graph_edit",
                "blueprint_block_replace",
                "blueprint_create", "blueprint_compile", "blueprint_save",
                "blueprint_component_edit", "blueprint_default_edit", "blueprint_member_edit",
                "widget_tree_edit", "gameplay_framework_edit", "game_data_inspect", "game_data_edit",
            ]:
                raise AssertionError("released command catalog mismatch")
            if capabilities.get("bridge_version") != __version__ or state.get("bridge_ready") is not True:
                raise AssertionError("capability/state contract mismatch")
            if capabilities.get("features", {}).get("graceful_editor_shutdown") is not True:
                raise AssertionError("graceful editor shutdown capability is unavailable")
            if capabilities.get("features", {}).get("blueprint_mutation") is not True:
                raise AssertionError("Phase 6 mutation capability is unavailable")
            for feature in ("blueprint_functions", "blueprint_local_variables", "blueprint_rep_notify"):
                if capabilities.get("features", {}).get(feature) is not True:
                    raise AssertionError(f"Phase 6 capability is unavailable: {feature}")
            for feature in (
                "blueprint_function_replacement",
                "blueprint_function_replacement_scratch_preflight",
            ):
                if capabilities.get("features", {}).get(feature) is not True:
                    raise AssertionError(f"function replacement capability is unavailable: {feature}")
            for feature in ("blueprint_macros", "blueprint_custom_events"):
                if capabilities.get("features", {}).get(feature) is not True:
                    raise AssertionError(f"Phase 7 capability is unavailable: {feature}")
            if capabilities.get("features", {}).get("blueprint_action_catalog") is not True:
                raise AssertionError("Phase 10 action catalog capability is unavailable")
            for feature in ("blueprint_graph_mutation", "blueprint_graph_node_lifecycle"):
                if capabilities.get("features", {}).get(feature) is not True:
                    raise AssertionError(f"Phase 11 graph capability is unavailable: {feature}")
            for feature in ("blueprint_graph_pin_defaults", "blueprint_graph_direct_connections"):
                if capabilities.get("features", {}).get(feature) is not True:
                    raise AssertionError(f"Phase 12 graph capability is unavailable: {feature}")
            for feature in ("blueprint_graph_wildcard_specialization", "blueprint_graph_automatic_conversion"):
                if capabilities.get("features", {}).get(feature) is not True:
                    raise AssertionError(f"Phase 13 graph capability is unavailable: {feature}")
            for feature in ("blueprint_family_policy", "game_mode_families", "game_state_families"):
                if capabilities.get("features", {}).get(feature) is not True:
                    raise AssertionError(f"Phase 14 family capability is unavailable: {feature}")
            if capabilities.get("features", {}).get("game_instance_family") is not True:
                raise AssertionError("Phase 15 GameInstance capability is unavailable")
            if capabilities.get("features", {}).get("widget_blueprint_family") is not True \
                    or capabilities.get("features", {}).get("widget_tree_authoring") is not True:
                raise AssertionError("Widget Blueprint capability is unavailable")
            for feature in (
                "umg_layout_authoring", "umg_style_authoring",
                "umg_property_bindings", "umg_designer_events",
            ):
                if capabilities.get("features", {}).get(feature) is not True:
                    raise AssertionError(f"UMG authoring capability is unavailable: {feature}")
            for feature in ("multiplayer_blueprint_authoring", "custom_event_rpcs",
                            "typed_replication_settings", "gameplay_framework_assignment"):
                if capabilities.get("features", {}).get(feature) is not True:
                    raise AssertionError(f"Phase 16 multiplayer capability is unavailable: {feature}")
            for feature in ("user_defined_struct_authoring", "typed_data_tables", "game_data_batch_editing"):
                if capabilities.get("features", {}).get(feature) is not True:
                    raise AssertionError(f"Phase 17 game-data capability is unavailable: {feature}")
            for feature in ("level_discovery", "level_open", "level_snapshots"):
                if capabilities.get("features", {}).get(feature) is not True:
                    raise AssertionError(f"level-open capability is unavailable: {feature}")
            for feature in (
                "level_actor_inspection",
                "level_world_partition_descriptors",
                "level_targeted_actor_loading",
                "level_instance_properties",
            ):
                if capabilities.get("features", {}).get(feature) is not True:
                    raise AssertionError(f"level-inspect capability is unavailable: {feature}")
            for feature in (
                "level_management", "level_blank_creation", "level_template_creation",
                "level_world_settings", "level_map_deletion",
            ):
                if capabilities.get("features", {}).get(feature) is not True:
                    raise AssertionError(f"level-management capability is unavailable: {feature}")
            for feature in (
                "level_actor_editing", "level_actor_transactions",
                "level_package_save_verification",
            ):
                if capabilities.get("features", {}).get(feature) is not True:
                    raise AssertionError(f"level-edit capability is unavailable: {feature}")
            if capabilities.get("features", {}).get("level_world_partition_conversion") is not False:
                raise AssertionError("level World Partition conversion must remain unsupported")
            for feature in ("asset_reference_discovery", "asset_reference_live_memory"):
                if capabilities.get("features", {}).get(feature) is not True:
                    raise AssertionError(f"asset-references capability is unavailable: {feature}")
            if capabilities.get("features", {}).get("asset_delete") is not True \
                    or capabilities.get("features", {}).get("asset_delete_force") is not False \
                    or capabilities.get("features", {}).get("asset_delete_undo") is not False:
                raise AssertionError("asset-delete capability contract mismatch")
            family_matrix = capabilities.get("blueprint_families", [])
            if [record.get("family") for record in family_matrix] != [
                "actor", "game_mode_base", "game_mode", "game_state_base", "game_state", "game_instance",
                "widget",
            ]:
                raise AssertionError(f"Phase 15 family matrix mismatch: {family_matrix!r}")
            for record in family_matrix:
                operations = record.get("operations", {})
                assignable = record.get("family") in {"game_mode_base", "game_mode", "game_instance"}
                if operations.get("graph_edit") is not True or operations.get("parent_change") is not False \
                        or operations.get("project_settings_assignment") is not assignable:
                    raise AssertionError(f"Phase 16 operation matrix mismatch: {record!r}")
                if not isinstance(record.get("multiplayer", {}).get("rpc_modes"), list):
                    raise AssertionError(f"Phase 16 multiplayer matrix mismatch: {record!r}")
                family = record.get("family")
                if operations.get("components") != (family not in {"game_instance", "widget"}) \
                        or operations.get("widget_tree") != (family == "widget"):
                    raise AssertionError(f"Blueprint family operation matrix mismatch: {record!r}")
            expected_graph_limits = {
                "graph_nodes": 2048, "graph_pins_per_node": 256, "graph_coordinate": 1000000,
                "graph_links_per_pin": 64, "graph_automatic_conversion_nodes": 1, "pin_default_chars": 512,
            }
            if any(capabilities.get("limits", {}).get(name) != value for name, value in expected_graph_limits.items()):
                raise AssertionError(f"Phase 13 graph limits mismatch: {capabilities.get('limits')!r}")
            expected_replacement_limits = {
                "function_replacement_nodes": 64,
                "function_replacement_owned_nodes": 256,
                "function_replacement_locals": 64,
                "function_replacement_defaults": 128,
                "function_replacement_connections": 256,
            }
            if any(capabilities.get("limits", {}).get(name) != value
                   for name, value in expected_replacement_limits.items()):
                raise AssertionError(
                    f"function replacement limits mismatch: {capabilities.get('limits')!r}")
            expected_game_data_limits = {
                "game_data_fields": 64, "game_data_rows": 2048, "game_data_batch_rows": 64,
                "game_data_collection_items": 64, "game_data_depth": 4, "game_data_dependencies": 256,
            }
            if any(capabilities.get("limits", {}).get(name) != value for name, value in expected_game_data_limits.items()):
                raise AssertionError(f"Phase 17 game-data limits mismatch: {capabilities.get('limits')!r}")
            expected_widget_limits = {
                "widget_tree_widgets": 512,
                "widget_tree_depth": 32,
                "widget_named_slots": 256,
                "widget_defaults_per_widget": 16,
                "widget_changed_defaults": 1024,
                "widget_bindings": 256,
            }
            if any(capabilities.get("limits", {}).get(name) != value
                   for name, value in expected_widget_limits.items()):
                raise AssertionError(f"Widget-tree limits mismatch: {capabilities.get('limits')!r}")
            if capabilities.get("limits", {}).get("level_discovery_scan") != 2048 \
                    or capabilities.get("limits", {}).get("level_external_packages") != 2048:
                raise AssertionError(f"level-open limits mismatch: {capabilities.get('limits')!r}")
            if capabilities.get("limits", {}).get("level_setup_properties") != 16 \
                    or capabilities.get("limits", {}).get("level_owned_packages") != 2048:
                raise AssertionError(f"level-management limits mismatch: {capabilities.get('limits')!r}")
            expected_level_inspect_limits = {
                "level_actor_scan": 4096,
                "level_actor_records": 2048,
                "level_components": 64,
                "level_actor_tags": 64,
                "level_data_layers": 32,
                "level_targeted_loads": 1,
            }
            if any(capabilities.get("limits", {}).get(name) != value
                   for name, value in expected_level_inspect_limits.items()):
                raise AssertionError(
                    f"level-inspect limits mismatch: {capabilities.get('limits')!r}")
            expected_level_edit_limits = {
                "level_edit_operations": 32,
                "level_edit_actors": 64,
                "level_save_packages": 64,
            }
            if any(capabilities.get("limits", {}).get(name) != value
                   for name, value in expected_level_edit_limits.items()):
                raise AssertionError(
                    f"level-edit limits mismatch: {capabilities.get('limits')!r}")
            expected_asset_reference_limits = {
                "asset_reference_registry_candidates": 4096,
                "asset_reference_live_objects": 8192,
                "asset_reference_records": 2048,
                "asset_reference_assets_per_package": 64,
                "asset_reference_properties": 16,
                "asset_reference_retained_cursors": 8,
                "asset_reference_traversal_depth": 1,
            }
            if any(capabilities.get("limits", {}).get(name) != value
                   for name, value in expected_asset_reference_limits.items()):
                raise AssertionError(
                    f"asset-references limits mismatch: {capabilities.get('limits')!r}"
                )
            if capabilities.get("asset_access") != {
                "read_scope": "all_mounted_content",
                "mutation_scope": "project_content_and_local_project_plugins",
            }:
                raise AssertionError("asset access policy contract mismatch")
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
        process = subprocess.Popen(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT)
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
