"""Game-data authoring and restart scenarios."""

from __future__ import annotations

import time
import uuid

from unreal_editor_mcp.bridge import UnrealBridge
from unreal_editor_mcp.errors import BridgeError, ErrorCode
from unreal_editor_mcp.project import ProjectLayout

from scripts.asset_family_conformance import (
    CrossProcessFamilyFixture,
    verify_recovered_mutation,
    verify_restart_read_back,
)

from .operations import reconcile_operation, send_without_reading
from .pagination import collect_cursor_pages

def collect_game_data(bridge: UnrealBridge, arguments: dict[str, object]) -> dict[str, object]:
    """Consume one game-data cursor chain while retaining its first-page metadata."""
    result = bridge.call("game_data_inspect", arguments)
    snapshot = result.get("snapshot_id")
    def fetch(cursor: str) -> dict[str, object]:
        page = bridge.call("game_data_inspect", {"cursor": cursor, "page_size": 100})
        if page.get("snapshot_id") != snapshot:
            raise AssertionError("game-data cursor continuation changed snapshots")
        return page
    merged = dict(result)
    merged["records"] = collect_cursor_pages(result, fetch)
    merged.pop("next_cursor", None)
    return merged


def verify_asset_inspect_data_table(
    bridge: UnrealBridge,
    table_path: str,
    expected_snapshot: str,
) -> None:
    """Verify the common facade's Data Table root, row, and column views."""
    root = bridge.call("asset_inspect", {"asset_path": table_path, "page_size": 1})
    asset = root.get("asset", {})
    rows = root.get("rows", {})
    if asset.get("type") != "data_table" \
            or root.get("snapshot_id") != expected_snapshot \
            or root.get("data_table", {}).get("row_count") != 1 \
            or root.get("schema", {}).get("field_count") != 2 \
            or [item.get("name") for item in rows.get("index", [])] != ["Rifle"]:
        raise AssertionError(f"asset_inspect Data Table root mismatch: {root!r}")
    row = bridge.call("asset_inspect", {
        "asset_path": table_path,
        "selector": "rows/Rifle",
    })
    if row.get("snapshot_id") != expected_snapshot \
            or row.get("row", {}).get("values", {}).get("Damage") != 45:
        raise AssertionError(f"asset_inspect exact Data Table row mismatch: {row!r}")
    column = bridge.call("asset_inspect", {
        "asset_path": table_path,
        "selector": "columns/Damage",
        "page_size": 1,
        "page_index": 0,
    })
    values = column.get("values", [])
    if column.get("snapshot_id") != expected_snapshot or len(values) != 1 \
            or values[0].get("row") != "Rifle" or values[0].get("value") != 45:
        raise AssertionError(f"asset_inspect Data Table column mismatch: {column!r}")


def author_phase_seventeen_game_data(
    bridge: UnrealBridge,
    layout: ProjectLayout,
    bridge_instance_id: str,
) -> dict[str, object]:
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
    batch = verify_recovered_mutation(
        "game-data",
        status,
        expected_fields=((('changed_count',), 2),),
    )
    final_table = collect_game_data(bridge, {
        "target": "data_table", "asset_path": table_path,
    })
    rows = final_table.get("records", [])
    if len(rows) != 1 or rows[0].get("name") != "Rifle" \
            or rows[0].get("values", {}).get("Damage") != 45 \
            or rows[0].get("values", {}).get("AmmoType") != "Rifle" \
            or final_table.get("snapshot_id") != batch.get("snapshot_id"):
        raise AssertionError(f"Phase 17 batch read-back mismatch: {final_table!r}")
    verify_asset_inspect_data_table(bridge, table_path, final_table["snapshot_id"])
    return {
        "struct_path": struct_path,
        "struct_snapshot": dependent_struct["snapshot_id"],
        "table_path": table_path,
        "table_snapshot": final_table["snapshot_id"],
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

    verify_restart_read_back(
        bridge,
        CrossProcessFamilyFixture(
            family_id="game-data-struct",
            command="game_data_inspect",
            arguments={
                "target": "user_defined_struct",
                "asset_path": game_data["struct_path"],
                "page_size": 100,
            },
            expected_fields=(),
        ),
        game_data["struct_snapshot"],
    )
    verify_restart_read_back(
        bridge,
        CrossProcessFamilyFixture(
            family_id="game-data-table",
            command="game_data_inspect",
            arguments={
                "target": "data_table",
                "asset_path": game_data["table_path"],
                "page_size": 100,
            },
            expected_fields=(),
        ),
        game_data["table_snapshot"],
    )
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
    verify_asset_inspect_data_table(bridge, game_data["table_path"], game_data["table_snapshot"])
