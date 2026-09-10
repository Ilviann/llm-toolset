"""Read-only animation graph inspection across two clean editor processes."""

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

ASSET = "/Game/UnrealMCPTests/ABP_AnimationFixture.ABP_AnimationFixture"


def inspect_animation(bridge: UnrealBridge) -> dict[str, object]:
    capabilities = bridge.call("capabilities")
    if capabilities.get("features", {}).get("animation_blueprint_inspection") is not True:
        raise AssertionError("Animation inspection capability is missing")
    family = next(item for item in capabilities["blueprint_families"] if item["family"] == "animation")
    if any(value is not (name in {"discover", "inspect"}) for name, value in family["operations"].items()):
        raise AssertionError("Animation family must be inspection-only")
    discovered = bridge.call("blueprint_inspect", {
        "mode": "discover", "package_path": "/Game/UnrealMCPTests",
        "asset_name": "ABP_AnimationFixture", "page_size": 100,
    })
    if not any(record.get("blueprint_family") == "animation" and record.get("asset_path") == ASSET
               for record in discovered["records"]):
        raise AssertionError("Unloaded animation fixture was not discovered")
    listing = collect_inspection(bridge, {
        "mode": "inspect", "asset_path": ASSET,
        "sections": ["graphs"], "page_size": 2,
    })
    snapshot = listing["snapshot_id"]
    records = []
    for graph in listing["records"]:
        if graph["section"] != "graph" or "parameters" not in graph or "node_count" in graph:
            raise AssertionError("Animation listing is not a compact graph summary")
        page = collect_inspection(bridge, {
            "mode": "inspect", "asset_path": ASSET, "graph_id": graph["id"], "page_size": 2,
        })
        if page["blueprint_family"] != "animation" or page["snapshot_id"] != snapshot:
            raise AssertionError("Animation graph selection changed family or snapshot")
        records.extend(page["records"])
    named = collect_inspection(bridge, {
        "mode": "inspect", "asset_path": ASSET, "graph_name": "Idle", "page_size": 1,
    })
    if named["snapshot_id"] != snapshot or not any(item["section"] == "node" for item in named["records"]):
        raise AssertionError("Named graph inspection did not return paginated details")
    for section in ("nodes", "pins", "connections"):
        try:
            bridge.call("blueprint_inspect", {"mode": "inspect", "asset_path": ASSET, "sections": [section]})
        except BridgeError as error:
            if error.code != ErrorCode.INVALID_ARGUMENT:
                raise
        else:
            raise AssertionError("Unscoped animation graph details were accepted")
    graphs = [item for item in records if item["section"] == "graph"]
    graph_ids = {item["id"] for item in graphs}
    for item in records:
        for field in ("bound_graph_id", "custom_transition_graph_id", "parent_graph_id"):
            if item.get(field) and item[field] not in graph_ids:
                raise AssertionError("Animation graph relationship does not resolve to its exact identity")
    if not {"event", "animation", "state_machine", "animation_state", "transition"}.issubset(
        {item["kind"] for item in graphs}
    ):
        raise AssertionError("Animation graph kinds are incomplete")
    for graph in graphs:
        filtered = bridge.call("blueprint_inspect", {
            "mode": "inspect", "asset_path": ASSET, "graph_id": graph["id"],
            "sections": ["graphs", "nodes", "pins", "connections"], "page_size": 100,
        })
        expected = [item for item in records if item.get("graph_id") == graph["id"] or item == graph]
        if filtered["has_more"] or filtered["records"] != expected:
            raise AssertionError("Exact animation graph filter changed records")
    try:
        bridge.call("blueprint_compile", {
            "operation_id": uuid.uuid4().hex, "asset_path": ASSET, "expected_snapshot": snapshot,
        })
    except BridgeError as error:
        if error.code != ErrorCode.WRONG_TYPE:
            raise
    else:
        raise AssertionError("Animation compilation was unexpectedly admitted")
    return {"snapshot_id": snapshot, "records": records}


def run_animation_restart_integration(executable: Path, layout: ProjectLayout, environment: dict[str, str]) -> int:
    from .lifecycle import ROOT, run_automation, shutdown_editor, stop_editor, wait_until_ready

    fixture = layout.root / "Content/UnrealMCPTests/ABP_AnimationFixture.uasset"
    fixture.unlink(missing_ok=True)
    run_automation(executable, layout.descriptor, environment, "UnrealMCP.Animation.AnimationLiveFixture")
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
                current = inspect_animation(bridge)
                if previous is not None and current != previous:
                    raise AssertionError("Animation graph identities, values, or snapshot changed after restart")
                previous = current
                shutdown_editor(bridge, process)
            except Exception:
                log.seek(0)
                sys.stderr.buffer.write(log.read()[-32_000:])
                raise
            finally:
                stop_editor(process, bridge=bridge)
        if hashlib.sha256(fixture.read_bytes()).digest() != digest:
            raise AssertionError("Read-only animation inspection changed the saved asset")
    print("Integration passed: animation discovery, nested graphs, paging, mutation rejection, restart, and preservation")
    return 0
