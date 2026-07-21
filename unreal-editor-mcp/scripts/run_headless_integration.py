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
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from unreal_editor_mcp.bridge import BRIDGE_PATH, UnrealBridge  # noqa: E402
from unreal_editor_mcp.discovery import read_discovery  # noqa: E402
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
            if result.get("bridge_ready") is True and record.bridge_version == "0.2.1":
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


def run_automation(executable: Path, project: Path, environment: dict[str, str]) -> int:
    expected = (
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
    )
    command = [
        str(executable), str(project), "-unattended", "-nop4", "-nosplash", "-nullrhi",
        "-stdout", "-FullStdOutLogOutput",
        "-ExecCmds=Automation RunTests UnrealMCP;Quit",
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
    environment = dict(os.environ)
    environment["DEVELOPER_DIR"] = str(developer)
    if sys.argv[1:] == ["--automation-only"]:
        return run_automation(executable, layout.descriptor, environment)
    if sys.argv[1:]:
        raise SystemExit("usage: run_headless_integration.py [--automation-only]")
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
            if capabilities.get("commands") != ["capabilities", "editor_state", "blueprint_inspect"]:
                raise AssertionError("mutation or unknown command was registered")
            if capabilities.get("bridge_version") != "0.2.1" or state.get("bridge_ready") is not True:
                raise AssertionError("capability/state contract mismatch")
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
    print("Phase 2 integration passed: authenticated discovery/inspection, save-reload snapshot, loopback binding, and clean unload")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
