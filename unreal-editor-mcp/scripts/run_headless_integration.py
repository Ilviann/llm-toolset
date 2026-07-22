#!/usr/bin/env python3
"""Launch the disposable editor and verify the released native boundary."""

from __future__ import annotations

import http.client
import json
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from unreal_editor_mcp.bridge import BRIDGE_PATH, UnrealBridge  # noqa: E402
from unreal_editor_mcp.discovery import read_discovery, read_token  # noqa: E402
from unreal_editor_mcp.errors import BridgeError, ErrorCode  # noqa: E402
from unreal_editor_mcp.project import ProjectLayout  # noqa: E402


def required_path(name: str) -> Path:
    value = os.environ.get(name)
    if not value:
        raise SystemExit(f"{name} is required")
    path = Path(value).expanduser().resolve()
    if not path.exists():
        raise SystemExit(f"{name} does not exist: {path}")
    return path


def wait_until_ready(layout: ProjectLayout, process: subprocess.Popen[bytes], deadline: float) -> None:
    last_error = "discovery record not created"
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"Unreal Editor exited before bridge startup ({process.returncode})")
        try:
            record = read_discovery(layout)
            result = UnrealBridge(layout, timeout=2.0).call("capabilities")
            if result.get("bridge_ready") is True and record.bridge_version == "0.15.0":
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
            "X-Unreal-MCP-Version": "0.15.0",
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
        if status.get("state") in {"committed", "rejected", "cancelled", "outcome_unknown"}:
            return status
        time.sleep(0.05)
    raise TimeoutError("lost mutation response did not reach a retained terminal state")


def collect_inspection(bridge: UnrealBridge, arguments: dict[str, object]) -> dict[str, object]:
    """Consume one bounded inspection cursor chain without treating prose as a fixture."""
    result = bridge.call("blueprint_inspect", arguments)
    records = list(result.get("records", []))
    cursor = result.get("next_cursor")
    for _ in range(63):
        if not isinstance(cursor, str):
            merged = dict(result)
            merged["records"] = records
            merged.pop("next_cursor", None)
            return merged
        page = bridge.call("blueprint_inspect", {"cursor": cursor, "page_size": 100})
        records.extend(page.get("records", []))
        cursor = page.get("next_cursor")
    raise AssertionError("inspection exceeded the retained cursor-page bound")


def author_phase_fourteen_families(bridge: UnrealBridge) -> dict[str, dict[str, object]]:
    configs = {
        "game_mode_base": ("/Script/Engine.GameModeBase", "bUseSeamlessTravel", True,
                           "/Script/Engine.GameModeBase", "GetDefaultPawnClassForController"),
        "game_mode": ("/Script/Engine.GameMode", "bDelayedStart", True,
                      "/Script/Engine.GameMode", "GetMatchState"),
        "game_state_base": ("/Script/Engine.GameStateBase", "ServerWorldTimeSecondsUpdateFrequency", 0.25,
                            "/Script/Engine.GameStateBase", "GetServerWorldTimeSeconds"),
        "game_state": ("/Script/Engine.GameState", "ServerWorldTimeSecondsUpdateFrequency", 0.75,
                       "/Script/Engine.GameStateBase", "GetServerWorldTimeSeconds"),
    }
    authored: dict[str, dict[str, object]] = {}
    for family, (parent, property_name, property_value, callable_owner, callable_name) in configs.items():
        asset_name = "BP_" + "".join(part.title() for part in family.split("_"))
        package_path = f"/Game/UnrealMCPPhase14/{asset_name}"
        asset_path = f"{package_path}.{asset_name}"
        created = bridge.call("blueprint_create", {
            "operation_id": uuid.uuid4().hex,
            "parent_class": parent,
            "package_path": package_path,
        })
        if created.get("blueprint_family") != family or created.get("saved") is not True:
            raise AssertionError(f"{family} creation contract mismatch: {created!r}")
        capabilities = created.get("family_capabilities", {})
        if any(capabilities.get(name) is not True for name in
               ("class_defaults", "components", "event_graphs", "local_variables", "overrides")):
            raise AssertionError(f"{family} live capability contract mismatch: {capabilities!r}")
        edited = bridge.call("blueprint_default_edit", {
            "operation_id": uuid.uuid4().hex,
            "asset_path": asset_path,
            "expected_snapshot": created["snapshot_id"],
            "property_name": property_name,
            "value": property_value,
        })
        component = bridge.call("blueprint_component_edit", {
            "operation_id": uuid.uuid4().hex,
            "asset_path": asset_path,
            "expected_snapshot": edited["snapshot_id"],
            "operation": "add",
            "component_class": "/Script/Engine.RotatingMovementComponent",
            "name": "FamilyMovement",
        })
        function = bridge.call("blueprint_member_edit", {
            "operation_id": uuid.uuid4().hex,
            "asset_path": asset_path,
            "expected_snapshot": component["snapshot_id"],
            "target": "function",
            "operation": "add",
            "name": "FamilyLogic",
            "signature": {"access": "public", "pure": False, "const": False, "parameters": []},
        })
        function_id = function.get("function", {}).get("id")
        local = bridge.call("blueprint_member_edit", {
            "operation_id": uuid.uuid4().hex,
            "asset_path": asset_path,
            "expected_snapshot": function["snapshot_id"],
            "target": "local_variable",
            "operation": "add",
            "function_id": function_id,
            "name": "FamilyCounter",
            "type": {"category": "int", "container": "none"},
            "default": {"kind": "literal", "value": 14},
        })
        inspection = collect_inspection(bridge, {
            "mode": "inspect",
            "asset_path": asset_path,
            "sections": ["summary", "components", "class_defaults", "functions", "local_variables", "graphs"],
            "property_names": [property_name],
            "page_size": 100,
        })
        event_graphs = [record for record in inspection.get("records", [])
                        if record.get("section") == "graph" and record.get("kind") == "event"
                        and record.get("inherited") is False]
        if inspection.get("blueprint_family") != family or not event_graphs:
            raise AssertionError(f"{family} inspection contract mismatch: {inspection!r}")
        catalog = bridge.call("blueprint_action_catalog", {
            "asset_path": asset_path,
            "graph_id": event_graphs[0]["id"],
            "expected_snapshot": inspection["snapshot_id"],
            "node_family": "function_call",
            "owner_class": callable_owner,
            "function": callable_name,
            "limit": 5,
        })
        if catalog.get("blueprint_family") != family or not catalog.get("actions"):
            raise AssertionError(f"{family} framework action is unavailable: {catalog!r}")
        compiled = bridge.call("blueprint_compile", {
            "operation_id": uuid.uuid4().hex,
            "asset_path": asset_path,
            "expected_snapshot": local["snapshot_id"],
        })
        saved = bridge.call("blueprint_save", {
            "operation_id": uuid.uuid4().hex,
            "asset_path": asset_path,
            "expected_snapshot": compiled["snapshot_id"],
        })
        if saved.get("compile_succeeded") is not True or saved.get("package_dirty") is not False:
            raise AssertionError(f"{family} compile/save contract mismatch: {saved!r}")
        authored[family] = {
            "asset_path": asset_path,
            "snapshot_id": saved["snapshot_id"],
            "property_name": property_name,
            "property_value": property_value,
            "function_id": function_id,
            "local_id": local.get("local_variable", {}).get("id"),
            "callable_owner": callable_owner,
            "callable_name": callable_name,
        }
    return authored


def author_phase_fifteen_game_instance(bridge: UnrealBridge) -> dict[str, object]:
    asset_name = "BP_GameInstance"
    package_path = f"/Game/UnrealMCPPhase15/{asset_name}"
    asset_path = f"{package_path}.{asset_name}"
    created = bridge.call("blueprint_create", {
        "operation_id": uuid.uuid4().hex,
        "parent_class": "/Script/Engine.GameInstance",
        "package_path": package_path,
    })
    if created.get("blueprint_family") != "game_instance" or created.get("saved") is not True:
        raise AssertionError(f"GameInstance creation contract mismatch: {created!r}")
    family_capabilities = created.get("family_capabilities", {})
    if family_capabilities.get("components") is not False or any(
            family_capabilities.get(name) is not True
            for name in ("class_defaults", "event_graphs", "local_variables", "overrides")):
        raise AssertionError(f"GameInstance live capability contract mismatch: {family_capabilities!r}")
    graph_types = family_capabilities.get("graph_types", {})
    if any(graph_types.get(name) is not True for name in ("event", "function", "macro")):
        raise AssertionError(f"GameInstance graph capability contract mismatch: {graph_types!r}")

    try:
        bridge.call("blueprint_component_edit", {
            "operation_id": uuid.uuid4().hex,
            "asset_path": asset_path,
            "expected_snapshot": created["snapshot_id"],
            "operation": "add",
            "component_class": "/Script/Engine.RotatingMovementComponent",
            "name": "InvalidMovement",
        })
    except BridgeError as error:
        if error.code != ErrorCode.INVALID_COMPONENT:
            raise AssertionError(f"GameInstance component rejection changed: {error.as_dict()!r}") from error
    else:
        raise AssertionError("GameInstance unexpectedly accepted a component mutation")
    unchanged = bridge.call("blueprint_inspect", {
        "mode": "inspect", "asset_path": asset_path, "sections": ["summary"], "page_size": 10,
    })
    if unchanged.get("snapshot_id") != created.get("snapshot_id"):
        raise AssertionError("GameInstance component rejection changed the structural snapshot")

    member = bridge.call("blueprint_member_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": created["snapshot_id"],
        "operation": "add",
        "name": "SessionRegion",
        "type": {"category": "string", "container": "none"},
        "default": {"kind": "literal", "value": "offline"},
        "metadata": {
            "category": "Session", "instance_editable": True, "blueprint_visible": True,
        },
    })
    compiled_member = bridge.call("blueprint_compile", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": member["snapshot_id"],
    })
    edited_default = bridge.call("blueprint_default_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": compiled_member["snapshot_id"],
        "property_name": "SessionRegion",
        "value": "eu-central",
    })
    function = bridge.call("blueprint_member_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": edited_default["snapshot_id"],
        "target": "function",
        "operation": "add",
        "name": "ResetSession",
        "signature": {"access": "public", "pure": False, "const": False, "parameters": []},
    })
    function_id = function.get("function", {}).get("id")
    local = bridge.call("blueprint_member_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": function["snapshot_id"],
        "target": "local_variable",
        "operation": "add",
        "function_id": function_id,
        "name": "PreviousRegion",
        "type": {"category": "string", "container": "none"},
        "default": {"kind": "literal", "value": ""},
    })
    inspection = collect_inspection(bridge, {
        "mode": "inspect",
        "asset_path": asset_path,
        "sections": ["summary", "components", "class_defaults", "functions", "local_variables", "graphs"],
        "property_names": ["SessionRegion"],
        "page_size": 100,
    })
    records = inspection.get("records", [])
    summaries = [record for record in records if record.get("section") == "summary"]
    event_graphs = [record for record in records if record.get("section") == "graph"
                    and record.get("kind") == "event" and record.get("inherited") is False]
    if (inspection.get("blueprint_family") != "game_instance" or not event_graphs or len(summaries) != 1
            or summaries[0].get("actor_blueprint") is not False
            or any(record.get("section") == "component" for record in records)):
        raise AssertionError(f"GameInstance inspection contract mismatch: {inspection!r}")
    event_graph_id = event_graphs[0]["id"]
    callback_catalog = bridge.call("blueprint_action_catalog", {
        "asset_path": asset_path,
        "graph_id": event_graph_id,
        "expected_snapshot": local["snapshot_id"],
        "node_family": "event",
        "owner_class": "/Script/Engine.GameInstance",
        "function": "ReceiveInit",
        "limit": 5,
    })
    if callback_catalog.get("blueprint_family") != "game_instance" or not callback_catalog.get("actions"):
        raise AssertionError(f"GameInstance Init callback is unavailable: {callback_catalog!r}")
    callback = bridge.call("blueprint_graph_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": local["snapshot_id"],
        "operation": "add_node",
        "graph_id": event_graph_id,
        "action_id": callback_catalog["actions"][0]["action_id"],
        "position": {"x": 160, "y": 120},
    })
    callback_node_id = callback.get("changed", {}).get("node", {}).get("id")
    if callback.get("blueprint_family") != "game_instance" or not isinstance(callback_node_id, str):
        raise AssertionError(f"GameInstance callback graph edit contract mismatch: {callback!r}")
    compiled = bridge.call("blueprint_compile", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": callback["snapshot_id"],
    })
    saved = bridge.call("blueprint_save", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": compiled["snapshot_id"],
    })
    if saved.get("compile_succeeded") is not True or saved.get("package_dirty") is not False:
        raise AssertionError(f"GameInstance compile/save contract mismatch: {saved!r}")
    return {
        "asset_path": asset_path,
        "snapshot_id": saved["snapshot_id"],
        "member_id": member.get("member", {}).get("id"),
        "function_id": function_id,
        "local_id": local.get("local_variable", {}).get("id"),
        "event_graph_id": event_graph_id,
        "callback_node_id": callback_node_id,
    }


def stop_editor(process: subprocess.Popen[bytes], timeout: float = 30.0) -> None:
    if process.poll() is not None:
        return
    process.send_signal(signal.SIGTERM)
    try:
        process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=10)
        raise RuntimeError("Unreal Editor did not unload cleanly")


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
    else:
        leaf = test_filter.rsplit(".", 1)[-1]
        expected = (leaf,) if leaf in all_expected else ()
    command = [
        str(executable), str(project), "-unattended", "-nop4", "-nosplash", "-nullrhi",
        "-stdout", "-FullStdOutLogOutput", "-NoAssetRegistryCache",
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
        "-stdout", "-FullStdOutLogOutput", "-NoAssetRegistryCache",
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
    developer = required_path("UNREAL_MCP_DEVELOPER_DIR")
    executable = engine / "Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
    if not executable.is_file():
        raise SystemExit(f"Unreal Editor executable not found: {executable}")
    layout = ProjectLayout.resolve(project)
    phase_four_fixture = layout.root / "Content" / "UnrealMCPPhase4" / "BP_ComponentFixture.uasset"
    phase_four_fixture.unlink(missing_ok=True)
    phase_fourteen_dir = layout.root / "Content" / "UnrealMCPPhase14"
    for name in ("BP_GameModeBase", "BP_GameMode", "BP_GameStateBase", "BP_GameState"):
        (phase_fourteen_dir / f"{name}.uasset").unlink(missing_ok=True)
    phase_fifteen_fixture = layout.root / "Content" / "UnrealMCPPhase15" / "BP_GameInstance.uasset"
    phase_fifteen_fixture.unlink(missing_ok=True)
    environment = dict(os.environ)
    environment["DEVELOPER_DIR"] = str(developer)
    if sys.argv[1:] == ["--automation-only"]:
        return run_automation(executable, layout.descriptor, environment)
    if len(sys.argv) == 3 and sys.argv[1] == "--automation-filter":
        return run_automation(executable, layout.descriptor, environment, sys.argv[2])
    if sys.argv[1:]:
        raise SystemExit("usage: run_headless_integration.py [--automation-only | --automation-filter PREFIX]")
    expected_snapshot = prepare_phase_two_fixture(executable, layout.descriptor, environment)
    command = [
        str(executable), str(layout.descriptor), "-unattended", "-nop4", "-nosplash",
        "-nullrhi", "-nosound", "-NoAssetRegistryCache",
    ]
    with tempfile.TemporaryFile() as log:
        process = subprocess.Popen(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT)
        try:
            wait_until_ready(layout, process, time.monotonic() + 120.0)
            bridge = UnrealBridge(layout, timeout=3.0)
            capabilities = bridge.call("capabilities")
            state = bridge.call("editor_state")
            if capabilities.get("commands") != [
                "capabilities", "editor_state", "operation_status", "blueprint_inspect", "blueprint_action_catalog", "blueprint_graph_edit",
                "blueprint_create", "blueprint_compile", "blueprint_save",
                "blueprint_component_edit", "blueprint_default_edit", "blueprint_member_edit", "gameplay_framework_edit",
            ]:
                raise AssertionError("released command catalog mismatch")
            if capabilities.get("bridge_version") != "0.15.0" or state.get("bridge_ready") is not True:
                raise AssertionError("capability/state contract mismatch")
            if capabilities.get("features", {}).get("blueprint_mutation") is not True:
                raise AssertionError("Phase 6 mutation capability is unavailable")
            for feature in ("blueprint_functions", "blueprint_local_variables", "blueprint_rep_notify"):
                if capabilities.get("features", {}).get(feature) is not True:
                    raise AssertionError(f"Phase 6 capability is unavailable: {feature}")
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
            for feature in ("multiplayer_blueprint_authoring", "custom_event_rpcs",
                            "typed_replication_settings", "gameplay_framework_assignment"):
                if capabilities.get("features", {}).get(feature) is not True:
                    raise AssertionError(f"Phase 16 multiplayer capability is unavailable: {feature}")
            family_matrix = capabilities.get("blueprint_families", [])
            if [record.get("family") for record in family_matrix] != [
                "actor", "game_mode_base", "game_mode", "game_state_base", "game_state", "game_instance",
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
                if operations.get("components") != (record.get("family") != "game_instance"):
                    raise AssertionError(f"Phase 15 component matrix mismatch: {record!r}")
            expected_graph_limits = {
                "graph_nodes": 2048, "graph_pins_per_node": 256, "graph_coordinate": 1000000,
                "graph_links_per_pin": 64, "graph_automatic_conversion_nodes": 1, "pin_default_chars": 512,
            }
            if any(capabilities.get("limits", {}).get(name) != value for name, value in expected_graph_limits.items()):
                raise AssertionError(f"Phase 13 graph limits mismatch: {capabilities.get('limits')!r}")
            if capabilities.get("asset_access") != {
                "read_scope": "all_mounted_content",
                "mutation_scope": "project_content_and_local_project_plugins",
            }:
                raise AssertionError("asset access policy contract mismatch")
            discovery = bridge.call("blueprint_inspect", {
                "mode": "discover",
                "package_path": "/Game/UnrealMCPPhase2",
                "asset_name": "BP_InspectionFixture",
            })
            if not any(record.get("section") == "asset" for record in discovery.get("records", [])):
                raise AssertionError("saved Actor Blueprint was not discoverable after editor restart")
            inspection = bridge.call("blueprint_inspect", {
                "mode": "inspect",
                "asset_path": "/Game/UnrealMCPPhase2/BP_InspectionFixture.BP_InspectionFixture",
                "sections": ["summary", "parent_class", "compile_state", "components", "variables", "graphs", "nodes", "pins", "connections"],
                "page_size": 100,
            })
            if inspection.get("snapshot_id") != expected_snapshot:
                raise AssertionError("saved/reloaded Blueprint structural snapshot changed")
            found = {record.get("section") for record in inspection.get("records", [])}
            if not {"summary", "component", "variable", "graph", "node", "pin"}.issubset(found):
                raise AssertionError(f"live inspection omitted required structure: {sorted(found)!r}")
            created = bridge.call("blueprint_create", {
                "operation_id": uuid.uuid4().hex,
                "parent_class": "/Script/Engine.Actor",
                "package_path": "/Game/UnrealMCPPhase4/BP_ComponentFixture",
            })
            if created.get("compile_succeeded") is not True or created.get("saved") is not True:
                raise AssertionError(f"Actor Blueprint creation did not compile and save: {created!r}")
            if created.get("parent_class") != "/Script/Engine.Actor" or created.get("package_dirty") is not False:
                raise AssertionError(f"Actor Blueprint creation contract mismatch: {created!r}")
            phase_fourteen_families = author_phase_fourteen_families(bridge)
            phase_fifteen_game_instance = author_phase_fifteen_game_instance(bridge)
            asset_path = "/Game/UnrealMCPPhase4/BP_ComponentFixture.BP_ComponentFixture"
            created = bridge.call("blueprint_default_edit", {
                "operation_id": uuid.uuid4().hex,
                "asset_path": asset_path,
                "expected_snapshot": created["snapshot_id"],
                "replication_setting": "replicates",
                "value": True,
            })
            created = bridge.call("blueprint_default_edit", {
                "operation_id": uuid.uuid4().hex,
                "asset_path": asset_path,
                "expected_snapshot": created["snapshot_id"],
                "replication_setting": "replicate_movement",
                "value": True,
            })
            lost_operation_id = uuid.uuid4().hex
            lost_arguments = {
                "operation_id": lost_operation_id,
                "asset_path": asset_path,
                "expected_snapshot": created["snapshot_id"],
                "operation": "add",
                "component_class": "/Script/Engine.SceneComponent",
                "name": "SceneRoot",
            }
            send_without_reading(layout, "blueprint_component_edit", lost_arguments)
            reconciled = reconcile_operation(bridge, lost_operation_id, capabilities["bridge_instance_id"])
            if reconciled.get("state") != "committed" or not isinstance(reconciled.get("result"), dict):
                raise AssertionError(f"lost component mutation did not reconcile as committed: {reconciled!r}")
            root_result = reconciled["result"]
            root_id = root_result.get("changed", {}).get("component_id")
            if not isinstance(root_id, str) or len(root_id) != 32:
                raise AssertionError("reconciled component add omitted its stable identity")
            replay = bridge.call("blueprint_component_edit", lost_arguments)
            if replay.get("request_digest") != root_result.get("request_digest") or replay.get("snapshot_id") != root_result.get("snapshot_id"):
                raise AssertionError("same-ID replay did not return the retained component result")

            rooted = bridge.call("blueprint_component_edit", {
                "operation_id": uuid.uuid4().hex,
                "asset_path": asset_path,
                "expected_snapshot": root_result["snapshot_id"],
                "operation": "set_root",
                "component_id": root_id,
            })
            mesh = bridge.call("blueprint_component_edit", {
                "operation_id": uuid.uuid4().hex,
                "asset_path": asset_path,
                "expected_snapshot": rooted["snapshot_id"],
                "operation": "add",
                "component_class": "/Script/Engine.StaticMeshComponent",
                "name": "Visual",
                "parent_id": root_id,
            })
            movement = bridge.call("blueprint_component_edit", {
                "operation_id": uuid.uuid4().hex,
                "asset_path": asset_path,
                "expected_snapshot": mesh["snapshot_id"],
                "operation": "add",
                "component_class": "/Script/Engine.RotatingMovementComponent",
                "name": "Movement",
            })
            defaulted = bridge.call("blueprint_default_edit", {
                "operation_id": uuid.uuid4().hex,
                "asset_path": asset_path,
                "expected_snapshot": movement["snapshot_id"],
                "property_name": "InitialLifeSpan",
                "value": 12.5,
            })
            member = bridge.call("blueprint_member_edit", {
                "operation_id": uuid.uuid4().hex,
                "asset_path": asset_path,
                "expected_snapshot": defaulted["snapshot_id"],
                "operation": "add",
                "name": "Health",
                "type": {"category": "int", "container": "none"},
                "default": {"kind": "literal", "value": 100},
                "metadata": {
                    "category": "Stats",
                    "tooltip": "Current health",
                    "instance_editable": True,
                    "blueprint_visible": True,
                    "save_game": True,
                    "replication": "replicated",
                },
            })
            member_id = member.get("member", {}).get("id")
            if not isinstance(member_id, str) or len(member_id) != 32:
                raise AssertionError(f"member mutation omitted its stable identity: {member!r}")
            function = bridge.call("blueprint_member_edit", {
                "operation_id": uuid.uuid4().hex,
                "asset_path": asset_path,
                "expected_snapshot": member["snapshot_id"],
                "target": "function",
                "operation": "add",
                "name": "ComputeHealth",
                "signature": {
                    "access": "public",
                    "pure": False,
                    "const": True,
                    "parameters": [
                        {"name": "Delta", "direction": "input", "type": {"category": "int", "container": "none"},
                         "default": {"kind": "literal", "value": 1}},
                        {"name": "Result", "direction": "output", "type": {"category": "int", "container": "none"}},
                    ],
                },
                "metadata": {"category": "Stats", "tooltip": "Compute a health value"},
            })
            function_id = function.get("function", {}).get("id")
            if not isinstance(function_id, str) or len(function_id) != 32:
                raise AssertionError(f"function mutation omitted its stable identity: {function!r}")
            local = bridge.call("blueprint_member_edit", {
                "operation_id": uuid.uuid4().hex,
                "asset_path": asset_path,
                "expected_snapshot": function["snapshot_id"],
                "target": "local_variable",
                "operation": "add",
                "function_id": function_id,
                "name": "WorkingValue",
                "type": {"category": "int", "container": "none"},
                "default": {"kind": "literal", "value": 5},
            })
            local_id = local.get("local_variable", {}).get("id")
            if not isinstance(local_id, str) or len(local_id) != 32:
                raise AssertionError(f"local-variable mutation omitted its stable identity: {local!r}")
            notify = bridge.call("blueprint_member_edit", {
                "operation_id": uuid.uuid4().hex,
                "asset_path": asset_path,
                "expected_snapshot": local["snapshot_id"],
                "target": "function",
                "operation": "add",
                "name": "OnRep_Health",
                "signature": {"access": "private", "pure": False, "const": False, "parameters": []},
            })
            notify_id = notify.get("function", {}).get("id")
            notified_member = bridge.call("blueprint_member_edit", {
                "operation_id": uuid.uuid4().hex,
                "asset_path": asset_path,
                "expected_snapshot": notify["snapshot_id"],
                "operation": "update",
                "member_id": member_id,
                "field": "metadata",
                "metadata": {
                    "replication": "rep_notify",
                    "rep_notify_function": "OnRep_Health",
                    "replication_condition": "COND_OwnerOnly",
                },
            })
            if notified_member.get("member", {}).get("replication", {}).get("rep_notify_function_id") != notify_id:
                raise AssertionError(f"RepNotify relationship did not bind the function identity: {notified_member!r}")
            graph_inspection = bridge.call("blueprint_inspect", {
                "mode": "inspect",
                "asset_path": asset_path,
                "sections": ["graphs"],
                "page_size": 100,
            })
            event_graphs = [
                record for record in graph_inspection.get("records", [])
                if record.get("section") == "graph" and record.get("kind") == "event" and record.get("inherited") is False
            ]
            if not event_graphs or not isinstance(event_graphs[0].get("id"), str):
                raise AssertionError(f"local event graph identity is unavailable: {event_graphs!r}")
            macro = bridge.call("blueprint_member_edit", {
                "operation_id": uuid.uuid4().hex,
                "asset_path": asset_path,
                "expected_snapshot": notified_member["snapshot_id"],
                "target": "macro",
                "operation": "add",
                "name": "ClampHealth",
                "signature": {
                    "pure": True,
                    "parameters": [
                        {"name": "Value", "direction": "input", "type": {"category": "int", "container": "none"},
                         "default": {"kind": "literal", "value": 100}},
                        {"name": "Result", "direction": "output", "type": {"category": "int", "container": "none"}},
                    ],
                },
                "metadata": {"category": "Stats", "tooltip": "Clamp one health value"},
            })
            macro_id = macro.get("macro", {}).get("id")
            if not isinstance(macro_id, str) or len(macro_id) != 32:
                raise AssertionError(f"macro mutation omitted its stable identity: {macro!r}")
            custom_event = bridge.call("blueprint_member_edit", {
                "operation_id": uuid.uuid4().hex,
                "asset_path": asset_path,
                "expected_snapshot": macro["snapshot_id"],
                "target": "custom_event",
                "operation": "add",
                "graph_id": event_graphs[0]["id"],
                "name": "OnHealthChanged",
                "signature": {
                    "parameters": [
                        {"name": "NewHealth", "type": {"category": "int", "container": "none"},
                         "default": {"kind": "literal", "value": 100}},
                    ],
                },
                "metadata": {"category": "Stats", "tooltip": "Health changed",
                             "rpc_mode": "server", "reliability": "reliable"},
            })
            custom_event_id = custom_event.get("custom_event", {}).get("id")
            if not isinstance(custom_event_id, str) or len(custom_event_id) != 32:
                raise AssertionError(f"custom-event mutation omitted its stable identity: {custom_event!r}")
            event_graph_id = event_graphs[0]["id"]
            graph_catalog = bridge.call("blueprint_action_catalog", {
                "asset_path": asset_path,
                "graph_id": event_graph_id,
                "expected_snapshot": custom_event["snapshot_id"],
                "member": "Health",
                "node_family": "variable_get",
                "limit": 5,
            })
            if not graph_catalog.get("actions"):
                raise AssertionError(f"Phase 11 retained node action is unavailable: {graph_catalog!r}")
            graph_add_operation = uuid.uuid4().hex
            graph_add_arguments = {
                "operation_id": graph_add_operation,
                "asset_path": asset_path,
                "expected_snapshot": custom_event["snapshot_id"],
                "operation": "add_node",
                "graph_id": event_graph_id,
                "action_id": graph_catalog["actions"][0]["action_id"],
                "position": {"x": 160, "y": 240},
            }
            send_without_reading(layout, "blueprint_graph_edit", graph_add_arguments)
            graph_add_status = reconcile_operation(bridge, graph_add_operation, capabilities["bridge_instance_id"])
            if graph_add_status.get("state") != "committed" or not isinstance(graph_add_status.get("result"), dict):
                raise AssertionError(f"lost node-add response did not reconcile: {graph_add_status!r}")
            graph_add = graph_add_status["result"]
            graph_node = graph_add.get("changed", {}).get("node", {})
            graph_node_id = graph_node.get("id")
            graph_pin_ids = [pin.get("id") for pin in graph_node.get("pins", [])]
            if (not isinstance(graph_node_id, str) or len(graph_node_id) != 32
                    or not graph_pin_ids or any(not isinstance(pin_id, str) or len(pin_id) != 32 for pin_id in graph_pin_ids)):
                raise AssertionError(f"node add omitted persistent node or pin identities: {graph_add!r}")

            graph_move_operation = uuid.uuid4().hex
            graph_move_arguments = {
                "operation_id": graph_move_operation,
                "asset_path": asset_path,
                "expected_snapshot": graph_add["snapshot_id"],
                "operation": "move_node",
                "graph_id": event_graph_id,
                "node_id": graph_node_id,
                "position": {"x": 480, "y": -160},
            }
            send_without_reading(layout, "blueprint_graph_edit", graph_move_arguments)
            graph_move_status = reconcile_operation(bridge, graph_move_operation, capabilities["bridge_instance_id"])
            graph_move = graph_move_status.get("result") if graph_move_status.get("state") == "committed" else None
            if not isinstance(graph_move, dict) or graph_move.get("changed", {}).get("node", {}).get("x") != 480:
                raise AssertionError(f"lost node-move response did not reconcile: {graph_move_status!r}")

            removal_catalog = bridge.call("blueprint_action_catalog", {
                "asset_path": asset_path,
                "graph_id": event_graph_id,
                "expected_snapshot": graph_move["snapshot_id"],
                "member": "Health",
                "node_family": "variable_get",
                "limit": 5,
            })
            if not removal_catalog.get("actions"):
                raise AssertionError(f"Phase 11 removal fixture action is unavailable: {removal_catalog!r}")
            temporary_add_operation = uuid.uuid4().hex
            temporary_add_arguments = {
                "operation_id": temporary_add_operation,
                "asset_path": asset_path,
                "expected_snapshot": graph_move["snapshot_id"],
                "operation": "add_node",
                "graph_id": event_graph_id,
                "action_id": removal_catalog["actions"][0]["action_id"],
                "position": {"x": 640, "y": 320},
            }
            send_without_reading(layout, "blueprint_graph_edit", temporary_add_arguments)
            temporary_add_status = reconcile_operation(bridge, temporary_add_operation, capabilities["bridge_instance_id"])
            temporary_add = temporary_add_status.get("result") if temporary_add_status.get("state") == "committed" else None
            temporary_node_id = temporary_add.get("changed", {}).get("node", {}).get("id") if isinstance(temporary_add, dict) else None
            if not isinstance(temporary_node_id, str):
                raise AssertionError(f"temporary node add did not reconcile: {temporary_add_status!r}")
            graph_remove_operation = uuid.uuid4().hex
            graph_remove_arguments = {
                "operation_id": graph_remove_operation,
                "asset_path": asset_path,
                "expected_snapshot": temporary_add["snapshot_id"],
                "operation": "remove_node",
                "graph_id": event_graph_id,
                "node_id": temporary_node_id,
            }
            send_without_reading(layout, "blueprint_graph_edit", graph_remove_arguments)
            graph_remove_status = reconcile_operation(bridge, graph_remove_operation, capabilities["bridge_instance_id"])
            graph_remove = graph_remove_status.get("result") if graph_remove_status.get("state") == "committed" else None
            if not isinstance(graph_remove, dict) or graph_remove.get("changed", {}).get("node", {}).get("id") != temporary_node_id:
                raise AssertionError(f"lost node-remove response did not reconcile: {graph_remove_status!r}")

            setter_catalog = bridge.call("blueprint_action_catalog", {
                "asset_path": asset_path,
                "graph_id": event_graph_id,
                "expected_snapshot": graph_remove["snapshot_id"],
                "member": "Health",
                "node_family": "variable_set",
                "limit": 5,
            })
            if not setter_catalog.get("actions"):
                raise AssertionError(f"Phase 12 setter action is unavailable: {setter_catalog!r}")
            setter_add_operation = uuid.uuid4().hex
            setter_add_arguments = {
                "operation_id": setter_add_operation,
                "asset_path": asset_path,
                "expected_snapshot": graph_remove["snapshot_id"],
                "operation": "add_node",
                "graph_id": event_graph_id,
                "action_id": setter_catalog["actions"][0]["action_id"],
                "position": {"x": 800, "y": 240},
            }
            send_without_reading(layout, "blueprint_graph_edit", setter_add_arguments)
            setter_add_status = reconcile_operation(bridge, setter_add_operation, capabilities["bridge_instance_id"])
            setter_add = setter_add_status.get("result") if setter_add_status.get("state") == "committed" else None
            setter_node_id = setter_add.get("changed", {}).get("node", {}).get("id") if isinstance(setter_add, dict) else None
            if not isinstance(setter_node_id, str):
                raise AssertionError(f"Phase 12 setter node did not reconcile: {setter_add_status!r}")
            pin_inspection = bridge.call("blueprint_inspect", {
                "mode": "inspect",
                "asset_path": asset_path,
                "sections": ["pins"],
                "graph_id": event_graph_id,
                "page_size": 100,
            })
            pins = [record for record in pin_inspection.get("records", []) if record.get("section") == "pin"]
            def exact_pin(node_id: str, direction: str, category: str, name: str | None = None) -> str:
                matches = [record for record in pins
                           if record.get("node_id") == node_id and record.get("direction") == direction
                           and record.get("type", {}).get("category") == category
                           and (name is None or record.get("name") == name)]
                if len(matches) != 1 or not isinstance(matches[0].get("id"), str):
                    raise AssertionError(f"expected one stable {node_id}/{direction}/{category}/{name} pin: {matches!r}")
                return matches[0]["id"]
            setter_exec_pin_id = exact_pin(setter_node_id, "input", "exec")
            setter_value_pin_id = exact_pin(setter_node_id, "input", "int", "Health")
            custom_event_exec_pin_id = exact_pin(custom_event_id, "output", "exec")

            pin_default_operation = uuid.uuid4().hex
            pin_default_arguments = {
                "operation_id": pin_default_operation,
                "asset_path": asset_path,
                "expected_snapshot": setter_add["snapshot_id"],
                "operation": "set_pin_default",
                "graph_id": event_graph_id,
                "node_id": setter_node_id,
                "pin_id": setter_value_pin_id,
                "default": {"kind": "literal", "value": 77},
            }
            send_without_reading(layout, "blueprint_graph_edit", pin_default_arguments)
            pin_default_status = reconcile_operation(bridge, pin_default_operation, capabilities["bridge_instance_id"])
            pin_default = pin_default_status.get("result") if pin_default_status.get("state") == "committed" else None
            if not isinstance(pin_default, dict) or pin_default.get("changed", {}).get("default") != {"kind": "literal", "value": 77}:
                raise AssertionError(f"lost pin-default response did not reconcile: {pin_default_status!r}")

            connection_arguments = {
                "asset_path": asset_path,
                "operation": "connect_pins",
                "graph_id": event_graph_id,
                "from_node_id": custom_event_id,
                "from_pin_id": custom_event_exec_pin_id,
                "to_node_id": setter_node_id,
                "to_pin_id": setter_exec_pin_id,
            }
            connect_operation = uuid.uuid4().hex
            send_without_reading(layout, "blueprint_graph_edit", {
                **connection_arguments, "operation_id": connect_operation,
                "expected_snapshot": pin_default["snapshot_id"],
            })
            connect_status = reconcile_operation(bridge, connect_operation, capabilities["bridge_instance_id"])
            connected = connect_status.get("result") if connect_status.get("state") == "committed" else None
            if not isinstance(connected, dict) or connected.get("changed", {}).get("connection", {}).get("connected") is not True:
                raise AssertionError(f"lost direct-connect response did not reconcile: {connect_status!r}")

            disconnect_operation = uuid.uuid4().hex
            send_without_reading(layout, "blueprint_graph_edit", {
                **connection_arguments, "operation": "disconnect_pins", "operation_id": disconnect_operation,
                "expected_snapshot": connected["snapshot_id"],
            })
            disconnect_status = reconcile_operation(bridge, disconnect_operation, capabilities["bridge_instance_id"])
            disconnected = disconnect_status.get("result") if disconnect_status.get("state") == "committed" else None
            if not isinstance(disconnected, dict) or disconnected.get("changed", {}).get("connection", {}).get("connected") is not False:
                raise AssertionError(f"lost direct-disconnect response did not reconcile: {disconnect_status!r}")

            reconnect_operation = uuid.uuid4().hex
            send_without_reading(layout, "blueprint_graph_edit", {
                **connection_arguments, "operation_id": reconnect_operation,
                "expected_snapshot": disconnected["snapshot_id"],
            })
            reconnect_status = reconcile_operation(bridge, reconnect_operation, capabilities["bridge_instance_id"])
            reconnected = reconnect_status.get("result") if reconnect_status.get("state") == "committed" else None
            if not isinstance(reconnected, dict) or reconnected.get("changed", {}).get("connection", {}).get("direct") is not True:
                raise AssertionError(f"lost direct-reconnect response did not reconcile: {reconnect_status!r}")

            def add_exact_action(snapshot: str, filters: dict[str, object], position: dict[str, int]) -> dict[str, object]:
                catalog = bridge.call("blueprint_action_catalog", {
                    "asset_path": asset_path,
                    "graph_id": event_graph_id,
                    "expected_snapshot": snapshot,
                    **filters,
                    "limit": 50,
                })
                actions = catalog.get("actions", [])
                if not actions:
                    raise AssertionError(f"Phase 13 action is unavailable for {filters!r}: {catalog!r}")
                return bridge.call("blueprint_graph_edit", {
                    "operation_id": uuid.uuid4().hex,
                    "asset_path": asset_path,
                    "expected_snapshot": snapshot,
                    "operation": "add_node",
                    "graph_id": event_graph_id,
                    "action_id": actions[0]["action_id"],
                    "position": position,
                })

            literal_add = add_exact_action(reconnected["snapshot_id"], {
                "node_family": "literal", "function": "MakeLiteralInt",
            }, {"x": 1120, "y": 240})
            literal_node_id = literal_add.get("changed", {}).get("node", {}).get("id")
            literal_output_pin_id = next((
                pin.get("id") for pin in literal_add.get("changed", {}).get("node", {}).get("pins", [])
                if pin.get("direction") == "output" and pin.get("type", {}).get("category") == "int"
            ), None)
            if not isinstance(literal_node_id, str) or not isinstance(literal_output_pin_id, str):
                raise AssertionError(f"Phase 13 literal identities are unavailable: {literal_add!r}")

            operator_catalog = bridge.call("blueprint_action_catalog", {
                "asset_path": asset_path,
                "graph_id": event_graph_id,
                "expected_snapshot": literal_add["snapshot_id"],
                "node_family": "operator",
                "pin_context": {"node_id": literal_node_id, "pin_id": literal_output_pin_id},
                "limit": 50,
            })
            wildcard_actions = [action for action in operator_catalog.get("actions", []) if action.get("wildcard") is True]
            wildcard_action = next((
                action for action in wildcard_actions
                if str(action.get("member_name", "")).casefold().startswith("add_")
                or str(action.get("title", "")).casefold().startswith("add")
            ), None)
            if wildcard_action is None:
                raise AssertionError(f"Phase 13 wildcard Add action is unavailable: {operator_catalog!r}")
            operator_add = bridge.call("blueprint_graph_edit", {
                "operation_id": uuid.uuid4().hex,
                "asset_path": asset_path,
                "expected_snapshot": literal_add["snapshot_id"],
                "operation": "add_node",
                "graph_id": event_graph_id,
                "action_id": wildcard_action["action_id"],
                "position": {"x": 1360, "y": 240},
            })
            operator_node_id = operator_add.get("changed", {}).get("node", {}).get("id")
            if not isinstance(operator_node_id, str):
                raise AssertionError(f"Phase 13 operator identity is unavailable: {operator_add!r}")

            print_add = add_exact_action(operator_add["snapshot_id"], {
                "node_family": "function_call",
                "owner_class": "/Script/Engine.KismetSystemLibrary",
                "function": "PrintString",
            }, {"x": 1840, "y": 240})
            print_node_id = print_add.get("changed", {}).get("node", {}).get("id")
            if not isinstance(print_node_id, str):
                raise AssertionError(f"Phase 13 PrintString identity is unavailable: {print_add!r}")

            existing_event_nodes = collect_inspection(bridge, {
                "mode": "inspect", "asset_path": asset_path, "sections": ["nodes"],
                "graph_id": event_graph_id, "page_size": 100,
            })
            begin_play_matches = [
                record for record in existing_event_nodes.get("records", [])
                if record.get("section") == "node"
                and record.get("class_path") == "/Script/BlueprintGraph.K2Node_Event"
                and "beginplay" in str(record.get("title", "")).replace(" ", "").casefold()
            ]
            if len(begin_play_matches) == 1:
                begin_play_node_id = begin_play_matches[0].get("id")
                begin_play_snapshot = print_add["snapshot_id"]
            else:
                begin_play_add = add_exact_action(print_add["snapshot_id"], {
                    "node_family": "event",
                    "owner_class": "/Script/Engine.Actor",
                    "function": "ReceiveBeginPlay",
                }, {"x": 1120, "y": 0})
                begin_play_node_id = begin_play_add.get("changed", {}).get("node", {}).get("id")
                begin_play_snapshot = begin_play_add["snapshot_id"]
            if not isinstance(begin_play_node_id, str):
                raise AssertionError(f"Phase 13 BeginPlay identity is unavailable: {begin_play_matches!r}")

            phase13_pins = collect_inspection(bridge, {
                "mode": "inspect", "asset_path": asset_path, "sections": ["pins"],
                "graph_id": event_graph_id, "page_size": 100,
            })
            phase13_pin_records = [record for record in phase13_pins.get("records", []) if record.get("section") == "pin"]
            def phase13_pin(node_id: str, direction: str, category: str, name: str | None = None) -> str:
                matches = [record for record in phase13_pin_records
                           if record.get("node_id") == node_id and record.get("direction") == direction
                           and record.get("type", {}).get("category") == category
                           and (name is None or record.get("name") == name)]
                if len(matches) != 1 or not isinstance(matches[0].get("id"), str):
                    raise AssertionError(f"Phase 13 pin is ambiguous: {node_id}/{direction}/{category}/{name}: {matches!r}")
                return matches[0]["id"]
            operator_input_pin_id = phase13_pin(operator_node_id, "input", "wildcard", "A")
            print_exec_pin_id = phase13_pin(print_node_id, "input", "exec")
            print_text_pin_id = phase13_pin(print_node_id, "input", "string", "InString")
            begin_play_exec_pin_id = phase13_pin(begin_play_node_id, "output", "exec")

            wildcard_operation = uuid.uuid4().hex
            send_without_reading(layout, "blueprint_graph_edit", {
                "operation_id": wildcard_operation,
                "asset_path": asset_path,
                "expected_snapshot": begin_play_snapshot,
                "operation": "connect_pins",
                "graph_id": event_graph_id,
                "from_node_id": literal_node_id,
                "from_pin_id": literal_output_pin_id,
                "to_node_id": operator_node_id,
                "to_pin_id": operator_input_pin_id,
            })
            wildcard_status = reconcile_operation(bridge, wildcard_operation, capabilities["bridge_instance_id"])
            wildcard_connected = wildcard_status.get("result") if wildcard_status.get("state") == "committed" else None
            wildcard_change = wildcard_connected.get("changed", {}).get("connection", {}) if isinstance(wildcard_connected, dict) else {}
            if (wildcard_change.get("wildcard_specialized") is not True
                    or not wildcard_connected.get("reconstructed_identities")):
                raise AssertionError(f"lost wildcard-specialization response did not reconcile: {wildcard_status!r}")

            specialized_pins = collect_inspection(bridge, {
                "mode": "inspect", "asset_path": asset_path, "sections": ["pins"],
                "graph_id": event_graph_id, "page_size": 100,
            })
            specialized_records = [record for record in specialized_pins.get("records", []) if record.get("section") == "pin"]
            operator_output_matches = [record for record in specialized_records
                                       if record.get("node_id") == operator_node_id and record.get("direction") == "output"
                                       and record.get("type", {}).get("category") == "int"]
            if len(operator_output_matches) != 1 or not isinstance(operator_output_matches[0].get("id"), str):
                operator_records = [record for record in specialized_records if record.get("node_id") == operator_node_id]
                raise AssertionError(f"specialized operator output is unavailable: {operator_records!r}")
            operator_output_pin_id = operator_output_matches[0]["id"]

            conversion_operation = uuid.uuid4().hex
            send_without_reading(layout, "blueprint_graph_edit", {
                "operation_id": conversion_operation,
                "asset_path": asset_path,
                "expected_snapshot": wildcard_connected["snapshot_id"],
                "operation": "connect_pins",
                "graph_id": event_graph_id,
                "from_node_id": operator_node_id,
                "from_pin_id": operator_output_pin_id,
                "to_node_id": print_node_id,
                "to_pin_id": print_text_pin_id,
                "automatic_conversion": True,
            })
            conversion_status = reconcile_operation(bridge, conversion_operation, capabilities["bridge_instance_id"])
            converted = conversion_status.get("result") if conversion_status.get("state") == "committed" else None
            conversion_change = converted.get("changed", {}).get("connection", {}) if isinstance(converted, dict) else {}
            conversion_nodes = converted.get("changed", {}).get("nodes", []) if isinstance(converted, dict) else []
            if (conversion_change.get("automatic_conversion") is not True
                    or conversion_change.get("conversion_node_count") != 1 or len(conversion_nodes) != 1
                    or not converted.get("created_identities")):
                raise AssertionError(f"lost automatic-conversion response did not reconcile: {conversion_status!r}")
            conversion_node_id = conversion_nodes[0].get("id")
            if not isinstance(conversion_node_id, str):
                raise AssertionError(f"conversion node identity is unavailable: {converted!r}")

            begin_play_operation = uuid.uuid4().hex
            send_without_reading(layout, "blueprint_graph_edit", {
                "operation_id": begin_play_operation,
                "asset_path": asset_path,
                "expected_snapshot": converted["snapshot_id"],
                "operation": "connect_pins",
                "graph_id": event_graph_id,
                "from_node_id": begin_play_node_id,
                "from_pin_id": begin_play_exec_pin_id,
                "to_node_id": print_node_id,
                "to_pin_id": print_exec_pin_id,
            })
            begin_play_status = reconcile_operation(bridge, begin_play_operation, capabilities["bridge_instance_id"])
            begin_play_connected = begin_play_status.get("result") if begin_play_status.get("state") == "committed" else None
            if not isinstance(begin_play_connected, dict) or begin_play_connected.get("changed", {}).get("connection", {}).get("direct") is not True:
                raise AssertionError(f"lost BeginPlay direct-link response did not reconcile: {begin_play_status!r}")
            compiled = bridge.call("blueprint_compile", {
                "operation_id": uuid.uuid4().hex,
                "asset_path": asset_path,
                "expected_snapshot": begin_play_connected["snapshot_id"],
            })
            if compiled.get("compile_succeeded") is not True or compiled.get("saved") is not False:
                raise AssertionError(f"explicit Blueprint compile contract mismatch: {compiled!r}")
            saved = bridge.call("blueprint_save", {
                "operation_id": uuid.uuid4().hex,
                "asset_path": asset_path,
                "expected_snapshot": compiled["snapshot_id"],
            })
            if saved.get("saved") is not True or saved.get("package_dirty") is not False:
                raise AssertionError(f"explicit Blueprint save contract mismatch: {saved!r}")
            created_snapshot = saved.get("snapshot_id")
            if not isinstance(created_snapshot, str) or len(created_snapshot) != 40:
                raise AssertionError("created Blueprint did not return a structural snapshot")
            if os.name == "posix" and layout.token_file.stat().st_mode & 0o077:
                raise AssertionError("bridge token permissions are broader than the owning user")
            assigned_game_mode_class = phase_fourteen_families["game_mode_base"]["asset_path"] + "_C"
            assigned_game_instance_class = phase_fifteen_game_instance["asset_path"] + "_C"
            bridge.call("gameplay_framework_edit", {
                "operation_id": uuid.uuid4().hex,
                "project_hash": capabilities["project_hash"],
                "setting": "default_game_mode",
                "class_path": assigned_game_mode_class,
                "expected_class": "/Script/Engine.GameModeBase",
            })
            bridge.call("gameplay_framework_edit", {
                "operation_id": uuid.uuid4().hex,
                "project_hash": capabilities["project_hash"],
                "setting": "default_game_instance",
                "class_path": assigned_game_instance_class,
                "expected_class": "/Script/Engine.GameInstance",
            })
            reject_bad_token(layout)
            verify_loopback_only(read_discovery(layout).port)
        except Exception:
            log.seek(0)
            sys.stderr.buffer.write(log.read()[-32_000:])
            raise
        finally:
            stop_editor(process)
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
            reloaded = collect_inspection(reloaded_bridge, {
                "mode": "inspect",
                "asset_path": "/Game/UnrealMCPPhase4/BP_ComponentFixture.BP_ComponentFixture",
                "sections": ["summary", "parent_class", "compile_state", "components", "class_defaults", "variables",
                             "functions", "macros", "custom_events", "parameters", "local_variables", "graphs", "nodes", "pins", "connections"],
                "property_names": ["InitialLifeSpan", "bReplicates", "bReplicateMovement"],
                "page_size": 100,
            })
            if reloaded.get("snapshot_id") != created_snapshot:
                raise AssertionError("created Blueprint snapshot changed after editor restart")
            for family, expected in phase_fourteen_families.items():
                family_reloaded = collect_inspection(reloaded_bridge, {
                    "mode": "inspect",
                    "asset_path": expected["asset_path"],
                    "sections": ["summary", "components", "class_defaults", "functions", "local_variables", "graphs"],
                    "property_names": [expected["property_name"]],
                    "page_size": 100,
                })
                if family_reloaded.get("blueprint_family") != family \
                        or family_reloaded.get("snapshot_id") != expected["snapshot_id"]:
                    raise AssertionError(f"{family} identity changed after restart: {family_reloaded!r}")
                records = family_reloaded.get("records", [])
                if not any(record.get("section") == "component" and record.get("name") == "FamilyMovement"
                           for record in records):
                    raise AssertionError(f"{family} component changed after restart")
                if not any(record.get("section") == "class_default"
                           and record.get("name") == expected["property_name"]
                           and record.get("value") == expected["property_value"] for record in records):
                    raise AssertionError(f"{family} default changed after restart")
                if not any(record.get("section") == "function" and record.get("id") == expected["function_id"]
                           for record in records):
                    raise AssertionError(f"{family} function changed after restart")
                if not any(record.get("section") == "local_variable" and record.get("id") == expected["local_id"]
                           for record in records):
                    raise AssertionError(f"{family} local variable changed after restart")
                event_graphs = [record for record in records if record.get("section") == "graph"
                                and record.get("kind") == "event" and record.get("inherited") is False]
                catalog = reloaded_bridge.call("blueprint_action_catalog", {
                    "asset_path": expected["asset_path"],
                    "graph_id": event_graphs[0]["id"],
                    "expected_snapshot": family_reloaded["snapshot_id"],
                    "node_family": "function_call",
                    "owner_class": expected["callable_owner"],
                    "function": expected["callable_name"],
                    "limit": 5,
                })
                if catalog.get("blueprint_family") != family or not catalog.get("actions"):
                    raise AssertionError(f"{family} action changed after restart: {catalog!r}")
            game_instance_reloaded = collect_inspection(reloaded_bridge, {
                "mode": "inspect",
                "asset_path": phase_fifteen_game_instance["asset_path"],
                "sections": ["summary", "components", "class_defaults", "variables", "functions",
                             "local_variables", "graphs", "nodes"],
                "property_names": ["SessionRegion"],
                "page_size": 100,
            })
            if game_instance_reloaded.get("blueprint_family") != "game_instance" \
                    or game_instance_reloaded.get("snapshot_id") != phase_fifteen_game_instance["snapshot_id"]:
                raise AssertionError(f"GameInstance identity changed after restart: {game_instance_reloaded!r}")
            game_instance_records = game_instance_reloaded.get("records", [])
            summaries = [record for record in game_instance_records if record.get("section") == "summary"]
            if len(summaries) != 1 or summaries[0].get("actor_blueprint") is not False \
                    or summaries[0].get("family_capabilities", {}).get("components") is not False:
                raise AssertionError(f"GameInstance capabilities changed after restart: {summaries!r}")
            if any(record.get("section") == "component" for record in game_instance_records):
                raise AssertionError("GameInstance gained a component after restart")
            if not any(record.get("section") == "class_default" and record.get("name") == "SessionRegion"
                       and record.get("value") == "eu-central" for record in game_instance_records):
                raise AssertionError("GameInstance session default changed after restart")
            if not any(record.get("section") == "variable" and record.get("id") == phase_fifteen_game_instance["member_id"]
                       for record in game_instance_records):
                raise AssertionError("GameInstance member identity changed after restart")
            if not any(record.get("section") == "function" and record.get("id") == phase_fifteen_game_instance["function_id"]
                       for record in game_instance_records):
                raise AssertionError("GameInstance function identity changed after restart")
            if not any(record.get("section") == "local_variable" and record.get("id") == phase_fifteen_game_instance["local_id"]
                       for record in game_instance_records):
                raise AssertionError("GameInstance local identity changed after restart")
            if not any(record.get("section") == "node" and record.get("id") == phase_fifteen_game_instance["callback_node_id"]
                       for record in game_instance_records):
                raise AssertionError("GameInstance Init callback changed after restart")
            shutdown_catalog = reloaded_bridge.call("blueprint_action_catalog", {
                "asset_path": phase_fifteen_game_instance["asset_path"],
                "graph_id": phase_fifteen_game_instance["event_graph_id"],
                "expected_snapshot": game_instance_reloaded["snapshot_id"],
                "node_family": "event",
                "owner_class": "/Script/Engine.GameInstance",
                "function": "ReceiveShutdown",
                "limit": 5,
            })
            if shutdown_catalog.get("blueprint_family") != "game_instance" or not shutdown_catalog.get("actions"):
                raise AssertionError(f"GameInstance Shutdown callback changed after restart: {shutdown_catalog!r}")
            parent_records = [record for record in reloaded.get("records", []) if record.get("section") == "parent_class"]
            if len(parent_records) != 1 or parent_records[0].get("class_path") != "/Script/Engine.Actor":
                raise AssertionError("created Blueprint parent changed after editor restart")
            compile_records = [record for record in reloaded.get("records", []) if record.get("section") == "compile_state"]
            if len(compile_records) != 1 or compile_records[0].get("state") not in {"up_to_date", "up_to_date_with_warnings"}:
                raise AssertionError("created Blueprint did not reload in a compiled state")
            components = {
                record.get("name"): record
                for record in reloaded.get("records", [])
                if record.get("section") == "component" and record.get("ownership") == "local"
            }
            if not {"SceneRoot", "Visual", "Movement"}.issubset(components):
                raise AssertionError(f"created component hierarchy changed after restart: {sorted(components)!r}")
            if components["SceneRoot"].get("root") is not True:
                raise AssertionError("saved scene root was not restored as root")
            if components["Visual"].get("parent_id") != components["SceneRoot"].get("id"):
                raise AssertionError("saved scene attachment changed after restart")
            defaults = [
                record for record in reloaded.get("records", [])
                if record.get("section") == "class_default" and record.get("name") == "InitialLifeSpan"
            ]
            if len(defaults) != 1 or defaults[0].get("value") != 12.5:
                raise AssertionError(f"edited Actor class default changed after restart: {defaults!r}")
            members = [
                record for record in reloaded.get("records", [])
                if record.get("section") == "variable" and record.get("name") == "Health"
            ]
            if len(members) != 1 or members[0].get("id") != member_id:
                raise AssertionError(f"member identity changed after restart: {members!r}")
            health = members[0]
            if health.get("type", {}).get("category") != "int" or health.get("default") != {"kind": "literal", "value": 100}:
                raise AssertionError(f"member type/default changed after restart: {health!r}")
            if health.get("metadata", {}).get("category") != "Stats" or health.get("metadata", {}).get("save_game") is not True:
                raise AssertionError(f"member metadata changed after restart: {health!r}")
            if health.get("replication", {}).get("mode") != "rep_notify" or health.get("replication", {}).get("relationship_valid") is not True:
                raise AssertionError(f"member replication changed after restart: {health!r}")
            functions = {
                record.get("name"): record
                for record in reloaded.get("records", [])
                if record.get("section") == "function"
            }
            if functions.get("ComputeHealth", {}).get("id") != function_id:
                raise AssertionError(f"function identity changed after restart: {functions!r}")
            if functions.get("OnRep_Health", {}).get("id") != notify_id:
                raise AssertionError(f"RepNotify function identity changed after restart: {functions!r}")
            parameters = [
                record for record in reloaded.get("records", [])
                if record.get("section") == "parameter" and record.get("function_id") == function_id
            ]
            if [(item.get("name"), item.get("direction")) for item in parameters] != [("Delta", "input"), ("Result", "output")]:
                raise AssertionError(f"function signature changed after restart: {parameters!r}")
            locals_ = [
                record for record in reloaded.get("records", [])
                if record.get("section") == "local_variable" and record.get("id") == local_id
            ]
            if len(locals_) != 1 or locals_[0].get("scope", {}).get("function_id") != function_id:
                raise AssertionError(f"local-variable scope changed after restart: {locals_!r}")
            macros = [
                record for record in reloaded.get("records", [])
                if record.get("section") == "macro" and record.get("id") == macro_id
            ]
            if len(macros) != 1 or macros[0].get("name") != "ClampHealth" or macros[0].get("required_nodes", {}).get("valid") is not True:
                raise AssertionError(f"macro shell changed after restart: {macros!r}")
            if macros[0].get("signature", {}).get("pure") is not True:
                raise AssertionError(f"macro signature changed after restart: {macros!r}")
            custom_events = [
                record for record in reloaded.get("records", [])
                if record.get("section") == "custom_event" and record.get("id") == custom_event_id
            ]
            if len(custom_events) != 1 or custom_events[0].get("name") != "OnHealthChanged":
                raise AssertionError(f"custom-event shell changed after restart: {custom_events!r}")
            if custom_events[0].get("graph_relationship", {}).get("graph_kind") != "event":
                raise AssertionError(f"custom-event graph relationship changed after restart: {custom_events!r}")
            if custom_events[0].get("metadata", {}).get("rpc_mode") != "server" \
                    or custom_events[0].get("metadata", {}).get("reliability") != "reliable":
                raise AssertionError(f"custom-event RPC semantics changed after restart: {custom_events!r}")
            nodes = {
                record.get("id"): record
                for record in reloaded.get("records", [])
                if record.get("section") == "node"
            }
            if graph_node_id not in nodes or nodes[graph_node_id].get("x") != 480 or nodes[graph_node_id].get("y") != -160:
                raise AssertionError(f"created/moved graph node changed after restart: {nodes.get(graph_node_id)!r}")
            if temporary_node_id in nodes:
                raise AssertionError("removed graph node returned after restart")
            reloaded_pin_ids = {
                record.get("id")
                for record in reloaded.get("records", [])
                if record.get("section") == "pin" and record.get("node_id") == graph_node_id
            }
            if set(graph_pin_ids) != reloaded_pin_ids:
                raise AssertionError(f"created pin identities changed after restart: {sorted(reloaded_pin_ids)!r}")
            setter_pins = {
                record.get("id"): record
                for record in reloaded.get("records", [])
                if record.get("section") == "pin" and record.get("node_id") == setter_node_id
            }
            if setter_value_pin_id not in setter_pins or setter_pins[setter_value_pin_id].get("default") != {"kind": "literal", "value": 77}:
                raise AssertionError(f"Phase 12 pin default changed after restart: {setter_pins.get(setter_value_pin_id)!r}")
            connections = [record for record in reloaded.get("records", []) if record.get("section") == "connection"]
            expected_connection = {
                "from_node_id": custom_event_id,
                "from_pin_id": custom_event_exec_pin_id,
                "to_node_id": setter_node_id,
                "to_pin_id": setter_exec_pin_id,
            }
            if not any(all(record.get(key) == value for key, value in expected_connection.items()) for record in connections):
                raise AssertionError(f"Phase 12 direct connection changed after restart: {connections!r}")
            for node_id, label in {
                literal_node_id: "literal",
                operator_node_id: "specialized operator",
                print_node_id: "PrintString",
                begin_play_node_id: "BeginPlay",
                conversion_node_id: "conversion",
            }.items():
                if node_id not in nodes:
                    raise AssertionError(f"Phase 13 {label} node changed after restart: {node_id!r}")
            def has_connection(from_node_id: str, to_node_id: str) -> bool:
                return any(record.get("from_node_id") == from_node_id and record.get("to_node_id") == to_node_id
                           for record in connections)
            if not has_connection(literal_node_id, operator_node_id):
                raise AssertionError("Phase 13 wildcard-specialized input link changed after restart")
            if not has_connection(operator_node_id, conversion_node_id) or not has_connection(conversion_node_id, print_node_id):
                raise AssertionError("Phase 13 explicit conversion path changed after restart")
            if not has_connection(begin_play_node_id, print_node_id):
                raise AssertionError("Phase 13 BeginPlay behavior link changed after restart")
            event_graphs = [
                record for record in reloaded.get("records", [])
                if record.get("section") == "graph" and record.get("kind") == "event" and record.get("inherited") is False
            ]
            if not event_graphs:
                raise AssertionError("reloaded Blueprint has no local event graph for action catalog")
            catalog_base = {
                "asset_path": "/Game/UnrealMCPPhase4/BP_ComponentFixture.BP_ComponentFixture",
                "graph_id": event_graphs[0]["id"],
                "expected_snapshot": reloaded["snapshot_id"],
            }
            variable_actions = UnrealBridge(layout, timeout=3.0).call("blueprint_action_catalog", {
                **catalog_base, "member": "Health", "node_family": "variable_get", "limit": 5,
            })
            if not any(action.get("member_name") == "Health" for action in variable_actions.get("actions", [])):
                raise AssertionError(f"Phase 10 variable action missing after restart: {variable_actions!r}")
            function_actions = UnrealBridge(layout, timeout=3.0).call("blueprint_action_catalog", {
                **catalog_base, "function": "ComputeHealth", "node_family": "function_call", "limit": 5,
            })
            if not any(action.get("member_name") == "ComputeHealth" for action in function_actions.get("actions", [])):
                raise AssertionError(f"Phase 10 function action missing after restart: {function_actions!r}")
            expanded_queries = {
                "event": {"node_family": "event"},
                "flow_control": {"node_family": "flow_control"},
                "cast": {"node_family": "cast", "owner_class": "/Script/Engine.Actor"},
                "literal": {"node_family": "literal", "function": "MakeLiteralInt"},
                "operator": {"node_family": "operator"},
            }
            for family, filters in expanded_queries.items():
                catalog = reloaded_bridge.call("blueprint_action_catalog", {
                    **catalog_base, **filters, "limit": 10,
                })
                actions = catalog.get("actions", [])
                if not actions or any(action.get("node_family") != family for action in actions):
                    raise AssertionError(f"Phase 10 {family} action missing after restart: {catalog!r}")
                if len(json.dumps(catalog, separators=(",", ":"))) > 32_768:
                    raise AssertionError(f"Phase 10 {family} catalog exceeded representative context budget")
            operator_catalog = reloaded_bridge.call("blueprint_action_catalog", {
                **catalog_base, "node_family": "operator", "limit": 50,
            })
            if not any(action.get("wildcard") is True for action in operator_catalog.get("actions", [])):
                raise AssertionError(f"Phase 10 wildcard operator action missing: {operator_catalog!r}")
        except Exception:
            log.seek(0)
            sys.stderr.buffer.write(log.read()[-32_000:])
            raise
        finally:
            stop_editor(process)
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
    print("Phase 16 integration passed: multiplayer Blueprint authoring, framework assignment/restore, restart, and clean unload")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
