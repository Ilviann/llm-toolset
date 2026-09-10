"""Blueprint Interface inspection and preservation across clean editor restarts."""

from __future__ import annotations

import hashlib
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path

from unreal_editor_mcp.bridge import UnrealBridge
from unreal_editor_mcp.errors import BridgeError, ErrorCode
from unreal_editor_mcp.project import ProjectLayout
from .blueprint_restart_verification import collect_inspection

ASSET = "/Game/UnrealMCPTests/BPI_InterfaceFixture.BPI_InterfaceFixture"


def inspect_interface(bridge: UnrealBridge) -> dict[str, object]:
    capabilities = bridge.call("capabilities")
    if capabilities.get("features", {}).get("blueprint_interface_inspection") is not True:
        raise AssertionError("Interface inspection capability is missing")
    family = next(item for item in capabilities["blueprint_families"] if item["family"] == "interface")
    if any(value is not (name in {"discover", "inspect"}) for name, value in family["operations"].items()):
        raise AssertionError("Interface family must be inspection-only")
    discovered = bridge.call("blueprint_inspect", {
        "mode": "discover", "package_path": "/Game/UnrealMCPTests",
        "asset_name": "BPI_InterfaceFixture", "page_size": 100,
    })
    if not any(record.get("blueprint_family") == "interface" and record.get("asset_path") == ASSET
               for record in discovered["records"]):
        raise AssertionError("Unloaded interface fixture was not discovered")
    summary = bridge.call("blueprint_inspect", {
        "mode": "inspect", "asset_path": ASSET, "sections": ["summary"],
    })
    if summary["records"][0]["was_loaded"] or summary["records"][0]["package_dirty"]:
        raise AssertionError("Discovery loaded the interface or inspection dirtied it")
    listing = collect_inspection(bridge, {
        "mode": "inspect", "asset_path": ASSET,
        "sections": ["functions", "parameters", "graphs"], "page_size": 1,
    })
    if listing["blueprint_family"] != "interface":
        raise AssertionError("Exact inspection lost the interface family")
    functions = {item["name"]: item for item in listing["records"] if item["section"] == "function"}
    if set(functions) != {"GetValue", "Notify"}:
        raise AssertionError("Interface declarations are incomplete")
    for name, function in functions.items():
        expected = [("Count", "input", "int")]
        if name == "GetValue":
            expected.append(("Value", "output", "int"))
        actual = [(item["name"], item["direction"], item["type"]["category"])
                  for item in function["signature"]["parameters"]]
        if actual != expected or function["editable"] or function["ownership"] != "interface":
            raise AssertionError("Interface signature or ownership mismatch")
        if function["replacement_boundary"]["replaceable"] or not function["required_nodes"]["valid"]:
            raise AssertionError("Interface declaration validity or replacement mismatch")
    details = []
    for graph in (item for item in listing["records"] if item["section"] == "graph"):
        page = collect_inspection(bridge, {
            "mode": "inspect", "asset_path": ASSET, "graph_id": graph["id"], "page_size": 1,
        })
        if page["snapshot_id"] != listing["snapshot_id"] or page["blueprint_family"] != "interface":
            raise AssertionError("Selected interface graph changed snapshot or family")
        if not any(item["section"] == "node" for item in page["records"]):
            raise AssertionError("Selected interface graph has no nodes")
        details.extend(page["records"])
    for operation in ("blueprint_compile", "blueprint_save"):
        try:
            bridge.call(operation, {
                "operation_id": uuid.uuid4().hex, "asset_path": ASSET,
                "expected_snapshot": listing["snapshot_id"],
            })
        except BridgeError as error:
            if error.code != ErrorCode.WRONG_TYPE:
                raise
        else:
            raise AssertionError("Interface mutation was admitted")
    return {"snapshot_id": listing["snapshot_id"], "records": listing["records"], "details": details}


def run_interface_restart_integration(executable: Path, layout: ProjectLayout, environment: dict[str, str]) -> int:
    from .lifecycle import ROOT, run_automation, shutdown_editor, stop_editor, wait_until_ready

    fixture = layout.root / "Content/UnrealMCPTests/BPI_InterfaceFixture.uasset"
    fixture.unlink(missing_ok=True)
    run_automation(executable, layout.descriptor, environment, "UnrealMCP.Interfaces.InterfaceLiveFixture")
    digest = hashlib.sha256(fixture.read_bytes()).digest()
    previous = None
    command = [str(executable), str(layout.descriptor), "-unattended", "-nop4", "-nosplash",
               "-nullrhi", "-nosound", "-nocrashreports", "-NoAssetRegistryCache"]
    for _ in range(2):
        with tempfile.TemporaryFile() as log:
            bridge = None
            process = subprocess.Popen(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT)
            try:
                wait_until_ready(layout, process, time.monotonic() + 120.0)
                bridge = UnrealBridge(layout, timeout=5.0)
                current = inspect_interface(bridge)
                if previous is not None and current != previous:
                    raise AssertionError("Interface identities, signatures, or snapshot changed after restart")
                previous = current
                shutdown_editor(bridge, process)
            except Exception:
                log.seek(0)
                sys.stderr.buffer.write(log.read()[-32_000:])
                raise
            finally:
                stop_editor(process, bridge=bridge)
        if hashlib.sha256(fixture.read_bytes()).digest() != digest:
            raise AssertionError("Read-only interface inspection changed the saved asset")
    print("Integration passed: interface discovery, signatures, graphs, paging, mutation rejection, restart, and preservation")
    return 0
