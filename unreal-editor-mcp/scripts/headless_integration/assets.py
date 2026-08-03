"""Asset-reference and deletion scenarios."""

from __future__ import annotations

import time
import uuid

from unreal_editor_mcp.bridge import UnrealBridge
from unreal_editor_mcp.errors import BridgeError, ErrorCode
from unreal_editor_mcp.project import ProjectLayout


_REGISTRY_SETTLE_SECONDS = 30.0


def _inspect_after_registry_settles(
    bridge: UnrealBridge,
    asset_path: str,
    *,
    page_size: int,
) -> dict[str, object]:
    """Retry only an in-progress Asset Registry scan within a fixed deadline."""
    deadline = time.monotonic() + _REGISTRY_SETTLE_SECONDS
    while True:
        result = bridge.call("asset_references", {
            "asset_path": asset_path,
            "page_size": page_size,
        })
        scans = result.get("scans", {})
        registry = [
            scans.get(name, {})
            for name in ("serialized", "management", "searchable_name")
        ]
        if all(scan.get("status") == "complete" for scan in registry):
            return result
        if not any(scan.get("status") == "stale" for scan in registry) \
                or time.monotonic() >= deadline:
            return result
        time.sleep(0.1)


def run_asset_scenario(
    bridge: UnrealBridge,
    layout: ProjectLayout,
    bridge_instance_id: str,
    game_data: dict[str, object],
) -> dict[str, str]:
    """Verify serialized references and reconcile deletion of an unreferenced asset."""
    from .lifecycle import reconcile_operation, send_without_reading

    reference_asset_path = game_data["struct_path"]
    table_path = game_data["table_path"]
    if not isinstance(reference_asset_path, str) or not isinstance(table_path, str):
        raise AssertionError(f"game-data asset paths are invalid: {game_data!r}")

    reference_page = _inspect_after_registry_settles(
        bridge, reference_asset_path, page_size=1,
    )
    reference_records = list(reference_page.get("records", []))
    reference_snapshot = reference_page.get("snapshot_id")
    if reference_page.get("target", {}).get("asset_path") != reference_asset_path \
            or not isinstance(reference_snapshot, str) or len(reference_snapshot) != 40:
        raise AssertionError(f"asset-references target/snapshot mismatch: {reference_page!r}")
    cursor = reference_page.get("next_cursor")
    while cursor:
        continued = bridge.call("asset_references", {"cursor": cursor, "page_size": 100})
        if continued.get("snapshot_id") != reference_snapshot:
            raise AssertionError("asset-references cursor changed the exact snapshot")
        reference_records.extend(continued.get("records", []))
        cursor = continued.get("next_cursor")
    if reference_page.get("scans", {}).get("serialized", {}).get("status") != "complete" \
            or not any(record.get("evidence") == "serialized"
                       and record.get("referencer_asset_path") == table_path
                       for record in reference_records):
        raise AssertionError(
            "asset-references did not find the serialized Data Table dependency: "
            f"{reference_page!r} {reference_records!r}"
        )

    disposable = bridge.call("game_data_edit", {
        "operation_id": uuid.uuid4().hex,
        "target": "data_table",
        "operation": "create",
        "asset_path": "/Game/UnrealMCPAssetDelete/DT_Disposable",
        "row_struct": reference_asset_path,
    })
    disposable_path = disposable["asset_path"]
    if not isinstance(disposable_path, str):
        raise AssertionError(f"disposable asset path is invalid: {disposable!r}")
    disposable_references = _inspect_after_registry_settles(
        bridge, disposable_path, page_size=100,
    )
    disposable_scans = disposable_references.get("scans", {})
    registry_scans = (
        disposable_scans.get(name, {})
        for name in ("serialized", "management", "searchable_name")
    )
    live_scan = disposable_scans.get("live_memory", {})
    if disposable_references.get("record_count") != 0 \
            or any(scan.get("status") != "complete" for scan in registry_scans) \
            or live_scan.get("status") not in {"complete", "truncated"} \
            or live_scan.get("unsupported") is True \
            or live_scan.get("stale") is True:
        raise AssertionError(f"disposable deletion target is not clean: {disposable_references!r}")

    delete_operation = uuid.uuid4().hex
    send_without_reading(layout, "asset_delete", {
        "operation_id": delete_operation,
        "asset_path": disposable_path,
        "expected_snapshot": disposable_references["snapshot_id"],
    })
    delete_status = reconcile_operation(bridge, delete_operation, bridge_instance_id)
    delete_state = delete_status.get("state")
    delete_result = delete_status.get("result") \
        if delete_state in {"committed", "partial"} else None
    if not isinstance(delete_result, dict) \
            or delete_result.get("undo_supported") is not False \
            or (delete_state == "committed" and delete_result.get("deleted") is not True) \
            or (delete_state == "partial" and delete_result.get("operation_state") != "partial"):
        raise AssertionError(f"lost asset-delete response did not reconcile: {delete_status!r}")
    _assert_asset_missing(bridge, disposable_path, "deleted asset still resolved before restart")
    return {
        "reference_asset_path": reference_asset_path,
        "reference_snapshot": reference_snapshot,
        "disposable_path": disposable_path,
    }


def verify_restarted_assets(
    bridge: UnrealBridge,
    game_data: dict[str, object],
    disposable_path: str,
) -> None:
    """Verify serialized-reference evidence and deletion persistence after restart."""
    reloaded_references = _inspect_after_registry_settles(
        bridge, game_data["struct_path"], page_size=100,
    )
    if reloaded_references.get("scans", {}).get("serialized", {}).get("status") != "complete" \
            or not any(record.get("evidence") == "serialized"
                       and record.get("referencer_asset_path") == game_data["table_path"]
                       for record in reloaded_references.get("records", [])):
        raise AssertionError(
            f"serialized asset references changed after restart: {reloaded_references!r}"
        )
    _assert_asset_missing(bridge, disposable_path, "deleted asset returned after editor restart")


def _assert_asset_missing(bridge: UnrealBridge, asset_path: str, message: str) -> None:
    try:
        bridge.call("asset_references", {
            "asset_path": asset_path,
            "page_size": 100,
        })
    except BridgeError as error:
        if error.code is not ErrorCode.NOT_FOUND:
            raise
    else:
        raise AssertionError(message)
