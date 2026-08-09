"""Level discovery, open, and inspection scenario."""

from __future__ import annotations

import time
import uuid

from unreal_editor_mcp.bridge import UnrealBridge
from unreal_editor_mcp.errors import BridgeError, ErrorCode
from unreal_editor_mcp.project import ProjectLayout

from .operations import call_when_ready, reconcile_operation, send_without_reading


def open_acceptance_level(
    bridge: UnrealBridge,
    layout: ProjectLayout,
    bridge_instance_id: str,
) -> dict[str, str]:
    """Discover, open, reconcile, replay, and read back the acceptance level."""
    discovery = bridge.call("level_inspect", {
        "mode": "discover",
        "package_path": "/Engine/Maps/Templates",
        "asset_name": "OpenWorld",
        "page_size": 1,
    })
    discovered_maps = discovery.get("records", [])
    if len(discovered_maps) != 1:
        raise AssertionError(f"mounted acceptance level was not discoverable: {discovery!r}")
    map_path = discovered_maps[0].get("map_path")
    if not isinstance(map_path, str):
        raise AssertionError(f"mounted acceptance level path is invalid: {discovery!r}")

    operation_id = uuid.uuid4().hex
    arguments = {"operation_id": operation_id, "map_path": map_path}
    send_without_reading(layout, "level_open", arguments)
    status = reconcile_operation(bridge, operation_id, bridge_instance_id)
    opened = status.get("result") if status.get("state") == "committed" else None
    if not isinstance(opened, dict) or opened.get("opened") is not True:
        raise AssertionError(f"lost level-open response did not reconcile: {status!r}")
    snapshot_id = opened.get("snapshot_id")
    if not isinstance(snapshot_id, str) or len(snapshot_id) != 40:
        raise AssertionError(f"opened level snapshot is invalid: {opened!r}")

    current = call_when_ready(bridge, "level_inspect", {"mode": "current"})
    records = current.get("records", [])
    if len(records) != 1 or records[0].get("map_path") != map_path \
            or records[0].get("mounted") is not True \
            or records[0].get("dirty") is not False \
            or current.get("snapshot_id") != snapshot_id:
        raise AssertionError(f"opened level read-back mismatch: open={opened!r}, current={current!r}")
    replay = call_when_ready(bridge, "level_open", arguments)
    if replay.get("request_digest") != opened.get("request_digest"):
        raise AssertionError("same-ID level-open replay did not return the retained result")

    map_id = records[0]["map_id"]
    actor_page = call_when_ready(bridge, "level_inspect", {
        "mode": "actors",
        "map_id": map_id,
        "expected_snapshot": snapshot_id,
        "page_size": 1,
    })
    actor_records = actor_page.get("records", [])
    if len(actor_records) != 1:
        raise AssertionError(f"acceptance level actor descriptors are unavailable: {actor_page!r}")
    if actor_page.get("has_more") is True:
        continuation = call_when_ready(bridge, "level_inspect", {
            "cursor": actor_page.get("next_cursor"),
            "page_size": 1,
        })
        if len(continuation.get("records", [])) != 1:
            raise AssertionError(f"actor descriptor continuation failed: {continuation!r}")

    region_page = call_when_ready(bridge, "level_inspect", {
        "mode": "actors",
        "map_id": map_id,
        "expected_snapshot": snapshot_id,
        "filters": {
            "region": {
                "min": {"x": -1000000000, "y": -1000000000, "z": -1000000000},
                "max": {"x": 1000000000, "y": 1000000000, "z": 1000000000},
            },
        },
        "page_size": 1,
    })
    if not region_page.get("records"):
        raise AssertionError(f"acceptance level region filter returned no bounded actors: {region_page!r}")

    loaded_page = call_when_ready(bridge, "level_inspect", {
        "mode": "actors",
        "map_id": map_id,
        "expected_snapshot": snapshot_id,
        "filters": {"loaded": True},
        "page_size": 100,
    })
    loaded_records = loaded_page.get("records", [])
    if not loaded_records:
        raise AssertionError(f"acceptance level has no exact live actor target: {loaded_page!r}")
    preferred = next(
        (
            record for record in loaded_records
            if any(name in record.get("class_path", "") for name in (
                "WorldDataLayers", "WorldPartitionMiniMap", "DefaultPhysicsVolume",
            ))
        ),
        loaded_records[0],
    )
    actor_id = preferred.get("actor_id")
    actor_inspection = call_when_ready(bridge, "level_inspect", {
        "mode": "actor",
        "map_id": map_id,
        "expected_snapshot": snapshot_id,
        "actor_id": actor_id,
        "property_names": ["Tags"],
        "page_size": 100,
    })
    sections = [record.get("section") for record in actor_inspection.get("records", [])]
    if sections.count("actor") != 1 or sections.count("property") != 1:
        raise AssertionError(f"exact actor/property inspection failed: {actor_inspection!r}")
    return {
        "map_path": map_path,
        "map_id": map_id,
        "snapshot_id": snapshot_id,
        "actor_id": actor_id,
    }

