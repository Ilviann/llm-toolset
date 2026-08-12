"""Disposable level-management and deletion scenarios."""

from __future__ import annotations

import time
import uuid

from unreal_editor_mcp.bridge import UnrealBridge
from unreal_editor_mcp.errors import BridgeError, ErrorCode
from unreal_editor_mcp.project import ProjectLayout

def manage_disposable_level(
    bridge: UnrealBridge,
    layout: ProjectLayout,
    bridge_instance_id: str,
) -> str:
    """Create, configure, reload, leave, and safely delete one exact map closure."""
    from .operations import reconcile_operation, send_without_reading

    def call_when_ready(command: str, arguments: dict[str, object]) -> dict[str, object]:
        deadline = time.monotonic() + 10.0
        while True:
            try:
                return bridge.call(command, arguments)
            except BridgeError as error:
                if error.code is not ErrorCode.EDITOR_UNAVAILABLE or time.monotonic() >= deadline:
                    raise
                time.sleep(0.1)

    current = call_when_ready("level_inspect", {"mode": "current"})
    current_record = current.get("records", [{}])[0]
    original_path = current_record.get("map_path")
    map_name = f"L_Managed_{uuid.uuid4().hex[:8]}"
    map_path = f"/Game/UnrealMCPLevelManagement/{map_name}.{map_name}"

    # Reconcile fixtures retained by an interrupted earlier integration run through
    # the same reference-aware map deletion contract exercised below.
    stale_maps = call_when_ready("level_inspect", {
        "mode": "discover",
        "package_path": "/Game/UnrealMCPLevelManagement",
        "page_size": 100,
    })
    if stale_maps.get("next_cursor"):
        raise AssertionError("disposable level cleanup exceeded its bounded discovery page")
    for stale_record in stale_maps.get("records", []):
        stale_path = stale_record.get("map_path")
        stale_name = stale_path.rsplit(".", 1)[-1] if isinstance(stale_path, str) else ""
        if stale_name != "L_Managed" and not stale_name.startswith("L_Managed_"):
            continue
        stale_references = call_when_ready(
            "asset_references", {"asset_path": stale_path, "page_size": 100})
        if stale_references.get("records"):
            raise AssertionError(f"stale disposable map is referenced: {stale_references!r}")
        for _ in range(20):
            try:
                call_when_ready("asset_delete", {
                    "operation_id": uuid.uuid4().hex,
                    "asset_path": stale_path,
                    "expected_snapshot": stale_references["snapshot_id"],
                })
                break
            except BridgeError as error:
                if error.code is not ErrorCode.BUSY:
                    raise
                time.sleep(0.1)
        else:
            raise AssertionError("stale disposable map deletion remained busy")
    operation_id = uuid.uuid4().hex
    create_arguments = {
        "operation_id": operation_id,
        "operation": "create",
        "destination_path": map_path,
        "source": {"kind": "blank"},
        "creation_options": {
            "world_partition": False,
            "world_partition_streaming": False,
            "external_actors": False,
        },
        "settings": [{"property_name": "KillZ", "value": -25000}],
        "open_after_create": False,
        "expected_current_snapshot": current["snapshot_id"],
    }
    send_without_reading(layout, "level_manage", create_arguments)
    status = reconcile_operation(bridge, operation_id, bridge_instance_id)
    created = status.get("result") if status.get("state") == "committed" else None
    if not isinstance(created, dict) or created.get("saved") is not True \
            or created.get("reload_verified") is not True \
            or created.get("current_map_preserved") is not True:
        raise AssertionError(f"lost level-create response did not reconcile: {status!r}")
    # Let the first post-save discovery heartbeat replace its file without racing
    # the next Windows-side discovery read.
    time.sleep(0.25)
    replay = call_when_ready("level_manage", create_arguments)
    if replay.get("request_digest") != created.get("request_digest"):
        raise AssertionError("same-ID level-create replay did not return the retained result")

    opened = call_when_ready("level_open", {"operation_id": uuid.uuid4().hex, "map_path": map_path})
    configured = call_when_ready("level_manage", {
        "operation_id": uuid.uuid4().hex,
        "operation": "configure",
        "map_path": map_path,
        "expected_current_snapshot": opened["snapshot_id"],
        "settings": [
            {"property_name": "WorldToMeters", "value": 250},
            {"property_name": "DefaultGameMode", "value": "/Script/Engine.GameModeBase"},
        ],
        "reload_after_save": True,
    })
    changed = {record.get("name"): record.get("value") for record in configured.get("changed_properties", [])}
    if configured.get("saved") is not True or configured.get("reload_verified") is not True \
            or changed != {"WorldToMeters": 250, "DefaultGameMode": "/Script/Engine.GameModeBase"}:
        raise AssertionError(f"level configuration persistence mismatch: {configured!r}")

    time.sleep(0.25)
    call_when_ready("level_open", {"operation_id": uuid.uuid4().hex, "map_path": original_path})
    references = call_when_ready("asset_references", {"asset_path": map_path, "page_size": 100})
    if references.get("records"):
        raise AssertionError(f"disposable map unexpectedly has external references: {references!r}")
    for _ in range(20):
        try:
            deleted = bridge.call("asset_delete", {
                "operation_id": uuid.uuid4().hex,
                "asset_path": map_path,
                "expected_snapshot": references["snapshot_id"],
            })
            break
        except BridgeError as error:
            if error.code not in {ErrorCode.BUSY, ErrorCode.EDITOR_UNAVAILABLE}:
                raise
            time.sleep(0.1)
    else:
        raise AssertionError("map deletion remained busy after bounded retry")
    if deleted.get("deleted") is not True or deleted.get("map_deletion") is not True \
            or deleted.get("package_closure_complete") is not True:
        raise AssertionError(f"map closure deletion was not verified: {deleted!r}")
    return map_path



def verify_restarted_level_deletion(bridge: UnrealBridge, map_path: str) -> None:
    """Prove a deleted map remains absent from registry discovery after restart."""
    discovered = bridge.call("level_inspect", {
        "mode": "discover",
        "package_path": "/Game/UnrealMCPLevelManagement",
        "asset_name": map_path.rsplit(".", 1)[-1],
        "page_size": 1,
    })
    if discovered.get("records"):
        raise AssertionError(f"deleted map reappeared after restart: {discovered!r}")
    try:
        bridge.call("asset_references", {"asset_path": map_path})
    except BridgeError as error:
        if error.code is not ErrorCode.NOT_FOUND:
            raise
    else:
        raise AssertionError("deleted map still resolved after restart")
