"""Game-data and level scenarios."""

from __future__ import annotations

import uuid
import time

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


def manage_disposable_level(
    bridge: UnrealBridge,
    layout: ProjectLayout,
    bridge_instance_id: str,
) -> str:
    """Create, configure, reload, leave, and safely delete one exact map closure."""
    from .lifecycle import reconcile_operation, send_without_reading

    current = bridge.call("level_inspect", {"mode": "current"})
    current_record = current.get("records", [{}])[0]
    original_path = current_record.get("map_path")
    map_name = f"L_Managed_{uuid.uuid4().hex[:8]}"
    map_path = f"/Game/UnrealMCPLevelManagement/{map_name}.{map_name}"

    def call_when_ready(command: str, arguments: dict[str, object]) -> dict[str, object]:
        deadline = time.monotonic() + 10.0
        while True:
            try:
                return bridge.call(command, arguments)
            except BridgeError as error:
                if error.code is not ErrorCode.EDITOR_UNAVAILABLE or time.monotonic() >= deadline:
                    raise
                time.sleep(0.1)

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


def author_level_edit_scenario(
    bridge: UnrealBridge,
    layout: ProjectLayout,
    bridge_instance_id: str,
) -> dict[str, object]:
    """Place a mixed actor batch, replay it, reject stale state, and reload-verify saving."""
    from .lifecycle import reconcile_operation, send_without_reading

    def call_when_ready(command: str, arguments: dict[str, object]) -> dict[str, object]:
        deadline = time.monotonic() + 60.0
        while True:
            try:
                return bridge.call(command, arguments)
            except BridgeError as error:
                if error.code not in {ErrorCode.EDITOR_UNAVAILABLE, ErrorCode.BUSY}:
                    raise
                if time.monotonic() >= deadline:
                    raise AssertionError(
                        f"{command} remained unavailable after bounded retry: "
                        f"{error.as_dict()!r}"
                    ) from error
                time.sleep(0.1)

    def transform(x: float, y: float, z: float, yaw: float = 0.0) -> dict[str, object]:
        return {
            "location": {"x": x, "y": y, "z": z},
            "rotation": {"pitch": 0.0, "yaw": yaw, "roll": 0.0},
            "scale": {"x": 1.0, "y": 1.0, "z": 1.0},
        }

    current = call_when_ready("level_inspect", {"mode": "current"})
    original_path = current.get("records", [{}])[0].get("map_path")
    if isinstance(original_path, str) and original_path.startswith("/Engine/"):
        try:
            call_when_ready("level_actor_edit", {
                "operation_id": uuid.uuid4().hex,
                "map_id": current["records"][0]["map_id"],
                "expected_snapshot": current["snapshot_id"],
                "operations": [{
                    "operation": "spawn",
                    "class_path": "/Script/Engine.StaticMeshActor",
                    "transform": transform(0.0, 0.0, 0.0),
                }],
            })
        except BridgeError as error:
            if error.code is not ErrorCode.MUTATION_SCOPE_DENIED:
                raise
        else:
            raise AssertionError("level actor editing accepted an Engine-content map")
    map_name = f"L_Edit_{uuid.uuid4().hex[:8]}"
    map_path = f"/Game/UnrealMCPLevelEdit/{map_name}.{map_name}"
    created = call_when_ready("level_manage", {
        "operation_id": uuid.uuid4().hex,
        "operation": "create",
        "destination_path": map_path,
        "source": {"kind": "blank"},
        "creation_options": {
            "world_partition": False,
            "world_partition_streaming": False,
            "external_actors": False,
        },
        "open_after_create": False,
        "expected_current_snapshot": current["snapshot_id"],
    })
    if created.get("saved") is not True or created.get("reload_verified") is not True \
            or created.get("map_path") != map_path:
        raise AssertionError(f"level-edit fixture creation failed: {created!r}")
    call_when_ready("level_open", {"operation_id": uuid.uuid4().hex, "map_path": map_path})
    opened = call_when_ready("level_inspect", {"mode": "current"})
    record = opened.get("records", [{}])[0]
    map_id = record.get("map_id")
    snapshot = opened.get("snapshot_id")

    blueprint_class = "/Game/UnrealMCPPhase4/BP_ComponentFixture.BP_ComponentFixture_C"
    spawn_arguments = {
        "operation_id": uuid.uuid4().hex,
        "map_id": map_id,
        "expected_snapshot": snapshot,
        "operations": [
            {"operation": "spawn", "class_path": blueprint_class,
             "transform": transform(0.0, 0.0, 100.0), "label": "MCP_BP_A",
             "tags": ["MCP", "Placed"]},
            {"operation": "spawn", "class_path": blueprint_class,
             "transform": transform(200.0, 0.0, 100.0), "label": "MCP_BP_B"},
            {"operation": "spawn", "class_path": blueprint_class,
             "transform": transform(400.0, 0.0, 100.0), "label": "MCP_BP_C"},
            {"operation": "spawn", "class_path": "/Script/Engine.StaticMeshActor",
             "transform": transform(600.0, 0.0, 100.0), "label": "MCP_Native"},
        ],
    }
    send_without_reading(layout, "level_actor_edit", spawn_arguments)
    status = reconcile_operation(bridge, spawn_arguments["operation_id"], bridge_instance_id)
    spawned = status.get("result") if status.get("state") == "committed" else None
    operations = spawned.get("operations", []) if isinstance(spawned, dict) else []
    if len(operations) != 4 or any(not item.get("actor_id") for item in operations):
        raise AssertionError(f"mixed level actor spawn did not reconcile: {status!r}")
    replay = call_when_ready("level_actor_edit", spawn_arguments)
    if replay.get("request_digest") != spawned.get("request_digest"):
        raise AssertionError("same-ID level actor batch replay changed its retained result")

    actor_ids = [item["actor_id"] for item in operations]
    try:
        call_when_ready("level_actor_edit", {
            "operation_id": uuid.uuid4().hex,
            "map_id": map_id,
            "expected_snapshot": snapshot,
            "operations": [{"operation": "label", "actor_id": actor_ids[0],
                            "label": "MCP_STALE"}],
        })
    except BridgeError as error:
        if error.code is not ErrorCode.STALE_PRECONDITION:
            raise
    else:
        raise AssertionError("level actor edit accepted a stale map snapshot")

    edited = call_when_ready("level_actor_edit", {
        "operation_id": uuid.uuid4().hex,
        "map_id": map_id,
        "expected_snapshot": spawned["snapshot_id"],
        "operations": [
            {"operation": "attach", "actor_id": actor_ids[0],
             "parent_actor_id": actor_ids[1]},
            {"operation": "folder", "actor_id": actor_ids[0], "folder": "MCP/Placed"},
            {"operation": "transform", "actor_id": actor_ids[2],
             "transform": transform(400.0, 200.0, 150.0, 45.0)},
            {"operation": "tags", "actor_id": actor_ids[3],
             "tags": ["MCP", "Native"]},
        ],
    })
    affected_packages = edited.get("affected_packages", [])
    if not affected_packages or edited.get("operation_count") != 4:
        raise AssertionError(f"level actor edit package/read-back mismatch: {edited!r}")

    expected = [
        {"actor_id": actor_ids[0], "label": "MCP_BP_A", "folder": "MCP/Placed",
         "tags": ["MCP", "Placed"], "transform": transform(0.0, 0.0, 100.0)},
        {"actor_id": actor_ids[1], "label": "MCP_BP_B",
         "transform": transform(200.0, 0.0, 100.0)},
        {"actor_id": actor_ids[2], "label": "MCP_BP_C",
         "transform": transform(400.0, 200.0, 150.0, 45.0)},
        {"actor_id": actor_ids[3], "label": "MCP_Native", "tags": ["MCP", "Native"],
         "transform": transform(600.0, 0.0, 100.0)},
    ]
    save_arguments = {
        "operation_id": uuid.uuid4().hex,
        "map_id": map_id,
        "expected_snapshot": edited["snapshot_id"],
        "affected_packages": affected_packages,
        "verification": {"mode": "reload", "actors": expected},
    }
    send_without_reading(layout, "level_save", save_arguments)
    save_status = reconcile_operation(bridge, save_arguments["operation_id"], bridge_instance_id)
    saved = save_status.get("result") if save_status.get("state") == "committed" else None
    if not isinstance(saved, dict) or saved.get("saved") is not True \
            or saved.get("reload_performed") is not True \
            or saved.get("verification_succeeded") is not True:
        raise AssertionError(f"level actor package save did not reconcile: {save_status!r}")
    save_replay = call_when_ready("level_save", save_arguments)
    if save_replay.get("request_digest") != saved.get("request_digest"):
        raise AssertionError("same-ID level save replay changed its retained result")
    settled = call_when_ready("level_inspect", {"mode": "current"})
    if settled.get("snapshot_id") != saved.get("snapshot_id"):
        raise AssertionError(f"level-edit reload did not settle on its verified snapshot: {settled!r}")
    return {
        "map_path": map_path,
        "original_path": original_path,
        "map_id": map_id,
        "snapshot_id": saved["snapshot_id"],
        "actors": expected,
    }


def verify_restarted_level_edit(bridge: UnrealBridge, scenario: dict[str, object]) -> None:
    """Verify actor identities after restart, then remove the disposable map closure."""
    opened = bridge.call("level_open", {
        "operation_id": uuid.uuid4().hex,
        "map_path": scenario["map_path"],
    })
    if opened.get("snapshot_id") != scenario["snapshot_id"]:
        raise AssertionError(f"level-edit snapshot changed across restart: {opened!r}")
    for expected in scenario["actors"]:
        page = bridge.call("level_inspect", {
            "mode": "actors",
            "map_id": scenario["map_id"],
            "expected_snapshot": scenario["snapshot_id"],
            "filters": {"actor_id": expected["actor_id"]},
            "page_size": 1,
        })
        records = page.get("records", [])
        if len(records) != 1 or records[0].get("label") != expected["label"]:
            raise AssertionError(f"level-edit actor changed across restart: {page!r}")

    bridge.call("level_open", {
        "operation_id": uuid.uuid4().hex,
        "map_path": scenario["original_path"],
    })
    references = bridge.call("asset_references", {
        "asset_path": scenario["map_path"], "page_size": 100,
    })
    if references.get("records"):
        raise AssertionError(f"disposable level-edit map is referenced: {references!r}")
    deleted = bridge.call("asset_delete", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": scenario["map_path"],
        "expected_snapshot": references["snapshot_id"],
    })
    if deleted.get("deleted") is not True or deleted.get("map_deletion") is not True:
        raise AssertionError(f"disposable level-edit map cleanup failed: {deleted!r}")


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
