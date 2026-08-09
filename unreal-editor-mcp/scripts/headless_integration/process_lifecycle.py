"""Retained Unreal Editor process resolution, readiness, and shutdown."""

from __future__ import annotations

import http.client
import json
import os
import signal
import socket
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping

from unreal_editor_mcp import __version__
from unreal_editor_mcp.bridge import BRIDGE_PATH, UnrealBridge
from unreal_editor_mcp.discovery import read_discovery
from unreal_editor_mcp.errors import BridgeError
from unreal_editor_mcp.project import ProjectLayout


@dataclass(frozen=True)
class EditorProcessConfig:
    executable: Path
    project: Path
    arguments: tuple[str, ...]
    working_directory: Path
    environment: Mapping[str, str]

    def command(self) -> list[str]:
        return [str(self.executable), str(self.project), *self.arguments]


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


def launch_editor(config: EditorProcessConfig, log: object) -> subprocess.Popen[bytes]:
    return subprocess.Popen(
        config.command(),
        cwd=config.working_directory,
        env=dict(config.environment),
        stdout=log,
        stderr=subprocess.STDOUT,
    )


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
        connection.request("POST", BRIDGE_PATH, body=b'{"command":"capabilities","arguments":{}}', headers={"Authorization": "Bearer " + "0" * 64, "Content-Type": "application/json"})
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
    peer.close()
    raise AssertionError(f"bridge unexpectedly accepted connections on {non_loopback}:{port}")


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


def stop_editor(process: subprocess.Popen[bytes], timeout: float = 30.0, bridge: UnrealBridge | None = None) -> None:
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
