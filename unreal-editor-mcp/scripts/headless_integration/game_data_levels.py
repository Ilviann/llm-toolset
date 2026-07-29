"""Game-data and level scenarios."""

from __future__ import annotations

import uuid

from unreal_editor_mcp.bridge import UnrealBridge
from unreal_editor_mcp.errors import BridgeError, ErrorCode
from unreal_editor_mcp.project import ProjectLayout


def collect_game_data(bridge: UnrealBridge, arguments: dict[str, object]) -> dict[str, object]:
    """Consume one game-data cursor chain while retaining its first-page metadata."""
    result = bridge.call("game_data_inspect", arguments)
    records = list(result.get("records", []))
    snapshot = result.get("snapshot_id")
    cursor = result.get("next_cursor")
    for _ in range(63):
        if not isinstance(cursor, str):
            merged = dict(result)
            merged["records"] = records
            merged.pop("next_cursor", None)
            return merged
        page = bridge.call("game_data_inspect", {"cursor": cursor, "page_size": 100})
        if page.get("snapshot_id") != snapshot:
            raise AssertionError("game-data cursor continuation changed snapshots")
        records.extend(page.get("records", []))
        cursor = page.get("next_cursor")
    raise AssertionError("game-data inspection exceeded the retained cursor-page bound")


def author_phase_seventeen_game_data(
    bridge: UnrealBridge,
    layout: ProjectLayout,
    bridge_instance_id: str,
) -> dict[str, object]:
    from .lifecycle import reconcile_operation, send_without_reading

    struct_package = "/Game/UnrealMCPPhase17/ST_WeaponStats"
    struct_path = struct_package + ".ST_WeaponStats"
    table_package = "/Game/UnrealMCPPhase17/DT_WeaponStats"
    table_path = table_package + ".DT_WeaponStats"
    created_struct = bridge.call("game_data_edit", {
        "operation_id": uuid.uuid4().hex,
        "target": "user_defined_struct",
        "operation": "create",
        "asset_path": struct_package,
        "members": [
            {
                "name": "Damage",
                "type": {"category": "int", "container": "none"},
                "default": {"kind": "literal", "value": 25},
            },
            {
                "name": "AmmoType",
                "type": {"category": "string", "container": "none"},
                "default": {"kind": "literal", "value": "Rifle"},
            },
        ],
    })
    if created_struct.get("saved") is not True or created_struct.get("dirty") is not False:
        raise AssertionError(f"Phase 17 schema creation contract mismatch: {created_struct!r}")
    struct_inspection = collect_game_data(bridge, {
        "target": "user_defined_struct", "asset_path": struct_path, "page_size": 1,
    })
    members = {record.get("name"): record for record in struct_inspection.get("records", [])}
    if set(members) != {"Damage", "AmmoType"} or not isinstance(members["Damage"].get("id"), str):
        raise AssertionError(f"Phase 17 schema inspection mismatch: {struct_inspection!r}")

    created_table = bridge.call("game_data_edit", {
        "operation_id": uuid.uuid4().hex,
        "target": "data_table",
        "operation": "create",
        "asset_path": table_package,
        "row_struct": struct_path,
        "rows": [
            {"row_name": "Pistol", "values": {"Damage": 22, "AmmoType": "Pistol"}},
            {"row_name": "Rifle", "values": {"Damage": 42, "AmmoType": "Rifle"}},
        ],
    })
    if created_table.get("saved") is not True or created_table.get("dirty") is not False:
        raise AssertionError(f"Phase 17 table creation contract mismatch: {created_table!r}")
    table_inspection = collect_game_data(bridge, {
        "target": "data_table", "asset_path": table_path, "page_size": 1,
    })
    if [record.get("name") for record in table_inspection.get("records", [])] != ["Pistol", "Rifle"]:
        raise AssertionError(f"Phase 17 table cursor ordering mismatch: {table_inspection!r}")
    filtered = collect_game_data(bridge, {
        "target": "data_table", "asset_path": table_path, "row_names": ["Rifle"], "page_size": 1,
    })
    if filtered.get("snapshot_id") != table_inspection.get("snapshot_id") \
            or [record.get("name") for record in filtered.get("records", [])] != ["Rifle"]:
        raise AssertionError(f"Phase 17 filtered snapshot mismatch: {filtered!r}")

    dependent_struct = collect_game_data(bridge, {
        "target": "user_defined_struct", "asset_path": struct_path,
    })
    try:
        bridge.call("game_data_edit", {
            "operation_id": uuid.uuid4().hex,
            "target": "user_defined_struct",
            "operation": "remove_member",
            "asset_path": struct_path,
            "expected_snapshot": dependent_struct["snapshot_id"],
            "member_id": members["Damage"]["id"],
            "policy": "reject_if_referenced",
        })
    except BridgeError as error:
        if error.code is not ErrorCode.REFERENCED_SCHEMA:
            raise
    else:
        raise AssertionError("Phase 17 destructive schema edit ignored its dependent Data Table")
    if collect_game_data(bridge, {
        "target": "user_defined_struct", "asset_path": struct_path,
    }).get("snapshot_id") != dependent_struct.get("snapshot_id"):
        raise AssertionError("Phase 17 referenced schema rejection changed the struct snapshot")

    operation_id = uuid.uuid4().hex
    send_without_reading(layout, "game_data_edit", {
        "operation_id": operation_id,
        "target": "data_table",
        "operation": "batch",
        "asset_path": table_path,
        "expected_snapshot": table_inspection["snapshot_id"],
        "upserts": [
            {"row_name": "Rifle", "values": {"Damage": 45}, "preserve_unspecified": True},
        ],
        "remove_rows": ["Pistol"],
    })
    status = reconcile_operation(bridge, operation_id, bridge_instance_id)
    batch = status.get("result") if status.get("state") == "committed" else None
    if not isinstance(batch, dict) or batch.get("changed_count") != 2:
        raise AssertionError(f"Phase 17 lost batch response did not reconcile: {status!r}")
    final_table = collect_game_data(bridge, {
        "target": "data_table", "asset_path": table_path,
    })
    rows = final_table.get("records", [])
    if len(rows) != 1 or rows[0].get("name") != "Rifle" \
            or rows[0].get("values", {}).get("Damage") != 45 \
            or rows[0].get("values", {}).get("AmmoType") != "Rifle" \
            or final_table.get("snapshot_id") != batch.get("snapshot_id"):
        raise AssertionError(f"Phase 17 batch read-back mismatch: {final_table!r}")
    return {
        "struct_path": struct_path,
        "struct_snapshot": dependent_struct["snapshot_id"],
        "table_path": table_path,
        "table_snapshot": final_table["snapshot_id"],
    }


def open_acceptance_level(
    bridge: UnrealBridge,
    layout: ProjectLayout,
    bridge_instance_id: str,
) -> dict[str, str]:
    """Discover, open, reconcile, replay, and read back the acceptance level."""
    from .lifecycle import reconcile_operation, send_without_reading

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

    current = bridge.call("level_inspect", {"mode": "current"})
    records = current.get("records", [])
    if len(records) != 1 or records[0].get("map_path") != map_path \
            or records[0].get("mounted") is not True \
            or records[0].get("dirty") is not False \
            or current.get("snapshot_id") != snapshot_id:
        raise AssertionError(f"opened level read-back mismatch: open={opened!r}, current={current!r}")
    replay = bridge.call("level_open", arguments)
    if replay.get("request_digest") != opened.get("request_digest"):
        raise AssertionError("same-ID level-open replay did not return the retained result")

    map_id = records[0]["map_id"]
    actor_page = bridge.call("level_inspect", {
        "mode": "actors",
        "map_id": map_id,
        "expected_snapshot": snapshot_id,
        "page_size": 1,
    })
    actor_records = actor_page.get("records", [])
    if len(actor_records) != 1:
        raise AssertionError(f"acceptance level actor descriptors are unavailable: {actor_page!r}")
    if actor_page.get("has_more") is True:
        continuation = bridge.call("level_inspect", {
            "cursor": actor_page.get("next_cursor"),
            "page_size": 1,
        })
        if len(continuation.get("records", [])) != 1:
            raise AssertionError(f"actor descriptor continuation failed: {continuation!r}")

    region_page = bridge.call("level_inspect", {
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

    loaded_page = bridge.call("level_inspect", {
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
    actor_inspection = bridge.call("level_inspect", {
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


def verify_restarted_game_data_and_level(
    bridge: UnrealBridge,
    game_data: dict[str, object],
    level: dict[str, str],
) -> None:
    """Verify level and typed game-data persistence after a clean editor restart."""
    reopened_level = bridge.call("level_open", {
        "operation_id": uuid.uuid4().hex,
        "map_path": level["map_path"],
    })
    if reopened_level.get("snapshot_id") != level["snapshot_id"]:
        raise AssertionError(f"reopened level snapshot changed across clean restart: {reopened_level!r}")
    current_level = bridge.call("level_inspect", {"mode": "current"})
    if current_level.get("snapshot_id") != level["snapshot_id"] \
            or current_level.get("records", [{}])[0].get("map_path") != level["map_path"]:
        raise AssertionError(f"current level snapshot changed across clean restart: {current_level!r}")
    restarted_actor = bridge.call("level_inspect", {
        "mode": "actors",
        "map_id": level["map_id"],
        "expected_snapshot": level["snapshot_id"],
        "filters": {"actor_id": level["actor_id"]},
        "page_size": 1,
    })
    if len(restarted_actor.get("records", [])) != 1 \
            or restarted_actor["records"][0].get("actor_id") != level["actor_id"]:
        raise AssertionError(
            f"actor identity changed across clean restart: {restarted_actor!r}")

    reloaded_struct = collect_game_data(bridge, {
        "target": "user_defined_struct",
        "asset_path": game_data["struct_path"],
    })
    reloaded_table = collect_game_data(bridge, {
        "target": "data_table",
        "asset_path": game_data["table_path"],
    })
    if reloaded_struct.get("snapshot_id") != game_data["struct_snapshot"] \
            or reloaded_table.get("snapshot_id") != game_data["table_snapshot"]:
        raise AssertionError("Phase 17 game-data snapshots changed after restart")
    rows = reloaded_table.get("records", [])
    if len(rows) != 1 or rows[0].get("name") != "Rifle" \
            or rows[0].get("values", {}).get("Damage") != 45 \
            or rows[0].get("values", {}).get("AmmoType") != "Rifle":
        raise AssertionError(f"Phase 17 typed rows changed after restart: {reloaded_table!r}")
