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
            if result.get("bridge_ready") is True and record.bridge_version == "0.5.0":
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
            "X-Unreal-MCP-Version": "0.5.0",
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
    )
    if test_filter == "UnrealMCP":
        expected = all_expected
    elif test_filter == "UnrealMCP.Phase4":
        expected = tuple(name for name in all_expected if name in {
            "ComponentAndDefaultEdits", "OperationLedger", "PropertyCodec",
        })
    elif test_filter == "UnrealMCP.Phase5":
        expected = tuple(name for name in all_expected if name in {"K2TypeCodec", "MemberVariables"})
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
                "capabilities", "editor_state", "operation_status", "blueprint_inspect",
                "blueprint_create", "blueprint_compile", "blueprint_save",
                "blueprint_component_edit", "blueprint_default_edit", "blueprint_member_edit",
            ]:
                raise AssertionError("released command catalog mismatch")
            if capabilities.get("bridge_version") != "0.5.0" or state.get("bridge_ready") is not True:
                raise AssertionError("capability/state contract mismatch")
            if capabilities.get("features", {}).get("blueprint_mutation") is not True:
                raise AssertionError("Phase 5 mutation capability is unavailable")
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
            asset_path = "/Game/UnrealMCPPhase4/BP_ComponentFixture.BP_ComponentFixture"
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
            compiled = bridge.call("blueprint_compile", {
                "operation_id": uuid.uuid4().hex,
                "asset_path": asset_path,
                "expected_snapshot": member["snapshot_id"],
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
            reloaded = UnrealBridge(layout, timeout=3.0).call("blueprint_inspect", {
                "mode": "inspect",
                "asset_path": "/Game/UnrealMCPPhase4/BP_ComponentFixture.BP_ComponentFixture",
                "sections": ["summary", "parent_class", "compile_state", "components", "class_defaults", "variables", "graphs", "nodes", "pins", "connections"],
                "property_names": ["InitialLifeSpan"],
                "page_size": 100,
            })
            if reloaded.get("snapshot_id") != created_snapshot:
                raise AssertionError("created Blueprint snapshot changed after editor restart")
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
            if health.get("replication", {}).get("mode") != "replicated":
                raise AssertionError(f"member replication changed after restart: {health!r}")
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
    print("Phase 5 integration passed: typed members, components/defaults, lost-response reconciliation, restart inspection, loopback binding, and clean unload")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
