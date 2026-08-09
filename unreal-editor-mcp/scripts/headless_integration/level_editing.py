"""Transactional level-editing and restart scenario."""

from __future__ import annotations

import time
import uuid

from unreal_editor_mcp.bridge import UnrealBridge
from unreal_editor_mcp.errors import BridgeError, ErrorCode
from unreal_editor_mcp.project import ProjectLayout

def author_level_edit_scenario(
    bridge: UnrealBridge,
    layout: ProjectLayout,
    bridge_instance_id: str,
) -> dict[str, object]:
    """Place a mixed actor batch, replay it, reject stale state, and reload-verify saving."""
    from .operations import reconcile_operation, send_without_reading

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


