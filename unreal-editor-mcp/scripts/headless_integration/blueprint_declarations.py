"""Author Blueprint family and member declarations for integration verification."""

from __future__ import annotations

import uuid

from unreal_editor_mcp.bridge import UnrealBridge
from unreal_editor_mcp.errors import BridgeError, ErrorCode
from unreal_editor_mcp.project import ProjectLayout

def author_blueprint_declarations(
    bridge: UnrealBridge,
    layout: ProjectLayout,
    capabilities: dict[str, object],
    created: dict[str, object],
) -> dict[str, object]:
    """Author the component, default, member, macro, and custom-event declarations."""
    from .operations import call_when_ready, reconcile_operation, send_without_reading

    asset_path = "/Game/UnrealMCPPhase4/BP_ComponentFixture.BP_ComponentFixture"
    created = bridge.call("blueprint_default_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": created["snapshot_id"],
        "replication_setting": "replicates",
        "value": True,
    })
    created = bridge.call("blueprint_default_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": created["snapshot_id"],
        "replication_setting": "replicate_movement",
        "value": True,
    })
    lost_operation_id = uuid.uuid4().hex
    lost_arguments = {
        "operation_id": lost_operation_id,
        "asset_path": asset_path,
        "expected_snapshot": created["snapshot_id"],
        "operation": "add",
        "component_class": "/Script/Engine.SceneComponent",
        "name": "SceneRoot",
    }
    send_without_reading(layout, "blueprint_component_edit", lost_arguments)
    reconciled = reconcile_operation(bridge, lost_operation_id, capabilities["bridge_instance_id"])
    if reconciled.get("state") != "committed" or not isinstance(reconciled.get("result"), dict):
        raise AssertionError(f"lost component mutation did not reconcile as committed: {reconciled!r}")
    root_result = reconciled["result"]
    root_id = root_result.get("changed", {}).get("component_id")
    if not isinstance(root_id, str) or len(root_id) != 32:
        raise AssertionError("reconciled component add omitted its stable identity")
    replay = bridge.call("blueprint_component_edit", lost_arguments)
    if replay.get("request_digest") != root_result.get("request_digest") or replay.get("snapshot_id") != root_result.get("snapshot_id"):
        raise AssertionError("same-ID replay did not return the retained component result")

    rooted = call_when_ready(bridge, "blueprint_component_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": root_result["snapshot_id"],
        "operation": "set_root",
        "component_id": root_id,
    })
    mesh = bridge.call("blueprint_component_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": rooted["snapshot_id"],
        "operation": "add",
        "component_class": "/Script/Engine.StaticMeshComponent",
        "name": "Visual",
        "parent_id": root_id,
    })
    movement = bridge.call("blueprint_component_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": mesh["snapshot_id"],
        "operation": "add",
        "component_class": "/Script/Engine.RotatingMovementComponent",
        "name": "Movement",
    })
    defaulted = bridge.call("blueprint_default_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": movement["snapshot_id"],
        "property_name": "InitialLifeSpan",
        "value": 12.5,
    })
    member = bridge.call("blueprint_member_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": defaulted["snapshot_id"],
        "operation": "add",
        "name": "Health",
        "type": {"category": "int", "container": "none"},
        "default": {"kind": "literal", "value": 100},
        "metadata": {
            "category": "Stats",
            "tooltip": "Current health",
            "instance_editable": True,
            "blueprint_visible": True,
            "save_game": True,
            "replication": "replicated",
        },
    })
    member_id = member.get("member", {}).get("id")
    if not isinstance(member_id, str) or len(member_id) != 32:
        raise AssertionError(f"member mutation omitted its stable identity: {member!r}")
    function = bridge.call("blueprint_member_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": member["snapshot_id"],
        "target": "function",
        "operation": "add",
        "name": "ComputeHealth",
        "signature": {
            "access": "public",
            "pure": False,
            "const": True,
            "parameters": [
                {"name": "Delta", "direction": "input", "type": {"category": "int", "container": "none"},
                 "default": {"kind": "literal", "value": 1}},
                {"name": "Result", "direction": "output", "type": {"category": "int", "container": "none"}},
            ],
        },
        "metadata": {"category": "Stats", "tooltip": "Compute a health value"},
    })
    function_id = function.get("function", {}).get("id")
    if not isinstance(function_id, str) or len(function_id) != 32:
        raise AssertionError(f"function mutation omitted its stable identity: {function!r}")
    local = bridge.call("blueprint_member_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": function["snapshot_id"],
        "target": "local_variable",
        "operation": "add",
        "function_id": function_id,
        "name": "WorkingValue",
        "type": {"category": "int", "container": "none"},
        "default": {"kind": "literal", "value": 5},
    })
    local_id = local.get("local_variable", {}).get("id")
    if not isinstance(local_id, str) or len(local_id) != 32:
        raise AssertionError(f"local-variable mutation omitted its stable identity: {local!r}")
    notify = bridge.call("blueprint_member_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": local["snapshot_id"],
        "target": "function",
        "operation": "add",
        "name": "OnRep_Health",
        "signature": {"access": "private", "pure": False, "const": False, "parameters": []},
    })
    notify_id = notify.get("function", {}).get("id")
    notified_member = bridge.call("blueprint_member_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": notify["snapshot_id"],
        "operation": "update",
        "member_id": member_id,
        "field": "metadata",
        "metadata": {
            "replication": "rep_notify",
            "rep_notify_function": "OnRep_Health",
            "replication_condition": "COND_OwnerOnly",
        },
    })
    if notified_member.get("member", {}).get("replication", {}).get("rep_notify_function_id") != notify_id:
        raise AssertionError(f"RepNotify relationship did not bind the function identity: {notified_member!r}")
    graph_inspection = bridge.call("asset_inspect", {
        "asset_path": asset_path,
        "selector": "event_graphs/EventGraph",
        "verbose": True,
    })
    event_graph_id = graph_inspection.get("graph", {}).get("debug", {}).get("graph_guid")
    if not isinstance(event_graph_id, str):
        raise AssertionError(f"local event graph identity is unavailable: {graph_inspection!r}")
    macro = bridge.call("blueprint_member_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": notified_member["snapshot_id"],
        "target": "macro",
        "operation": "add",
        "name": "ClampHealth",
        "signature": {
            "pure": True,
            "parameters": [
                {"name": "Value", "direction": "input", "type": {"category": "int", "container": "none"},
                 "default": {"kind": "literal", "value": 100}},
                {"name": "Result", "direction": "output", "type": {"category": "int", "container": "none"}},
            ],
        },
        "metadata": {"category": "Stats", "tooltip": "Clamp one health value"},
    })
    macro_id = macro.get("macro", {}).get("id")
    if not isinstance(macro_id, str) or len(macro_id) != 32:
        raise AssertionError(f"macro mutation omitted its stable identity: {macro!r}")
    custom_event = bridge.call("blueprint_member_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": macro["snapshot_id"],
        "target": "custom_event",
        "operation": "add",
        "graph_id": event_graph_id,
        "name": "OnHealthChanged",
        "signature": {
            "parameters": [
                {"name": "NewHealth", "type": {"category": "int", "container": "none"},
                 "default": {"kind": "literal", "value": 100}},
            ],
        },
        "metadata": {"category": "Stats", "tooltip": "Health changed",
                     "rpc_mode": "server", "reliability": "reliable"},
    })
    custom_event_id = custom_event.get("custom_event", {}).get("id")
    if not isinstance(custom_event_id, str) or len(custom_event_id) != 32:
        raise AssertionError(f"custom-event mutation omitted its stable identity: {custom_event!r}")
    return {
        "asset_path": asset_path,
        "custom_event": custom_event,
        "custom_event_id": custom_event_id,
        "event_graph_id": event_graph_id,
        "function_id": function_id,
        "local_id": local_id,
        "macro_id": macro_id,
        "member_id": member_id,
        "notify_id": notify_id,
    }


def author_phase_fourteen_families(bridge: UnrealBridge) -> dict[str, dict[str, object]]:
    configs = {
        "game_mode_base": ("/Script/Engine.GameModeBase", "bUseSeamlessTravel", True,
                           "/Script/Engine.GameModeBase", "GetDefaultPawnClassForController"),
        "game_mode": ("/Script/Engine.GameMode", "bDelayedStart", True,
                      "/Script/Engine.GameMode", "GetMatchState"),
        "game_state_base": ("/Script/Engine.GameStateBase", "ServerWorldTimeSecondsUpdateFrequency", 0.25,
                            "/Script/Engine.GameStateBase", "GetServerWorldTimeSeconds"),
        "game_state": ("/Script/Engine.GameState", "ServerWorldTimeSecondsUpdateFrequency", 0.75,
                       "/Script/Engine.GameStateBase", "GetServerWorldTimeSeconds"),
    }
    authored: dict[str, dict[str, object]] = {}
    for family, (parent, property_name, property_value, callable_owner, callable_name) in configs.items():
        asset_name = "BP_" + "".join(part.title() for part in family.split("_"))
        package_path = f"/Game/UnrealMCPPhase14/{asset_name}"
        asset_path = f"{package_path}.{asset_name}"
        created = bridge.call("blueprint_create", {
            "operation_id": uuid.uuid4().hex,
            "parent_class": parent,
            "package_path": package_path,
        })
        if created.get("blueprint_family") != family or created.get("saved") is not True:
            raise AssertionError(f"{family} creation contract mismatch: {created!r}")
        capabilities = created.get("family_capabilities", {})
        if any(capabilities.get(name) is not True for name in
               ("class_defaults", "components", "event_graphs", "local_variables", "overrides")):
            raise AssertionError(f"{family} live capability contract mismatch: {capabilities!r}")
        edited = bridge.call("blueprint_default_edit", {
            "operation_id": uuid.uuid4().hex,
            "asset_path": asset_path,
            "expected_snapshot": created["snapshot_id"],
            "property_name": property_name,
            "value": property_value,
        })
        component = bridge.call("blueprint_component_edit", {
            "operation_id": uuid.uuid4().hex,
            "asset_path": asset_path,
            "expected_snapshot": edited["snapshot_id"],
            "operation": "add",
            "component_class": "/Script/Engine.RotatingMovementComponent",
            "name": "FamilyMovement",
        })
        function = bridge.call("blueprint_member_edit", {
            "operation_id": uuid.uuid4().hex,
            "asset_path": asset_path,
            "expected_snapshot": component["snapshot_id"],
            "target": "function",
            "operation": "add",
            "name": "FamilyLogic",
            "signature": {"access": "public", "pure": False, "const": False, "parameters": []},
        })
        function_id = function.get("function", {}).get("id")
        local = bridge.call("blueprint_member_edit", {
            "operation_id": uuid.uuid4().hex,
            "asset_path": asset_path,
            "expected_snapshot": function["snapshot_id"],
            "target": "local_variable",
            "operation": "add",
            "function_id": function_id,
            "name": "FamilyCounter",
            "type": {"category": "int", "container": "none"},
            "default": {"kind": "literal", "value": 14},
        })
        inspection = bridge.call("asset_inspect", {"asset_path": asset_path})
        graph = bridge.call("asset_inspect", {
            "asset_path": asset_path, "selector": "event_graphs/EventGraph", "verbose": True,
        })
        event_graph_id = graph.get("graph", {}).get("debug", {}).get("graph_guid")
        expected_type = {
            "game_mode_base": "game_mode_base_blueprint", "game_mode": "game_mode_blueprint",
            "game_state_base": "game_state_base_blueprint", "game_state": "game_state_blueprint",
        }[family]
        if inspection.get("asset", {}).get("type") != expected_type or not isinstance(event_graph_id, str):
            raise AssertionError(f"{family} inspection contract mismatch: {inspection!r}")
        catalog = bridge.call("blueprint_action_catalog", {
            "asset_path": asset_path,
            "graph_id": event_graph_id,
            "expected_snapshot": inspection["snapshot_id"],
            "node_family": "function_call",
            "owner_class": callable_owner,
            "function": callable_name,
            "limit": 5,
        })
        if catalog.get("blueprint_family") != family or not catalog.get("actions"):
            raise AssertionError(f"{family} framework action is unavailable: {catalog!r}")
        compiled = bridge.call("blueprint_compile", {
            "operation_id": uuid.uuid4().hex,
            "asset_path": asset_path,
            "expected_snapshot": local["snapshot_id"],
        })
        saved = bridge.call("blueprint_save", {
            "operation_id": uuid.uuid4().hex,
            "asset_path": asset_path,
            "expected_snapshot": compiled["snapshot_id"],
        })
        if saved.get("compile_succeeded") is not True or saved.get("package_dirty") is not False:
            raise AssertionError(f"{family} compile/save contract mismatch: {saved!r}")
        authored[family] = {
            "asset_path": asset_path,
            "snapshot_id": saved["snapshot_id"],
            "property_name": property_name,
            "property_value": property_value,
            "function_id": function_id,
            "local_id": local.get("local_variable", {}).get("id"),
            "callable_owner": callable_owner,
            "callable_name": callable_name,
        }
    return authored


def author_phase_fifteen_game_instance(bridge: UnrealBridge) -> dict[str, object]:
    asset_name = "BP_GameInstance"
    package_path = f"/Game/UnrealMCPPhase15/{asset_name}"
    asset_path = f"{package_path}.{asset_name}"
    created = bridge.call("blueprint_create", {
        "operation_id": uuid.uuid4().hex,
        "parent_class": "/Script/Engine.GameInstance",
        "package_path": package_path,
    })
    if created.get("blueprint_family") != "game_instance" or created.get("saved") is not True:
        raise AssertionError(f"GameInstance creation contract mismatch: {created!r}")
    family_capabilities = created.get("family_capabilities", {})
    if family_capabilities.get("components") is not False or any(
            family_capabilities.get(name) is not True
            for name in ("class_defaults", "event_graphs", "local_variables", "overrides")):
        raise AssertionError(f"GameInstance live capability contract mismatch: {family_capabilities!r}")
    graph_types = family_capabilities.get("graph_types", {})
    if any(graph_types.get(name) is not True for name in ("event", "function", "macro")):
        raise AssertionError(f"GameInstance graph capability contract mismatch: {graph_types!r}")

    try:
        bridge.call("blueprint_component_edit", {
            "operation_id": uuid.uuid4().hex,
            "asset_path": asset_path,
            "expected_snapshot": created["snapshot_id"],
            "operation": "add",
            "component_class": "/Script/Engine.RotatingMovementComponent",
            "name": "InvalidMovement",
        })
    except BridgeError as error:
        if error.code != ErrorCode.INVALID_COMPONENT:
            raise AssertionError(f"GameInstance component rejection changed: {error.as_dict()!r}") from error
    else:
        raise AssertionError("GameInstance unexpectedly accepted a component mutation")
    unchanged = bridge.call("asset_inspect", {"asset_path": asset_path})
    if unchanged.get("snapshot_id") != created.get("snapshot_id"):
        raise AssertionError("GameInstance component rejection changed the structural snapshot")

    member = bridge.call("blueprint_member_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": created["snapshot_id"],
        "operation": "add",
        "name": "SessionRegion",
        "type": {"category": "string", "container": "none"},
        "default": {"kind": "literal", "value": "offline"},
        "metadata": {
            "category": "Session", "instance_editable": True, "blueprint_visible": True,
        },
    })
    compiled_member = bridge.call("blueprint_compile", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": member["snapshot_id"],
    })
    edited_default = bridge.call("blueprint_default_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": compiled_member["snapshot_id"],
        "property_name": "SessionRegion",
        "value": "eu-central",
    })
    function = bridge.call("blueprint_member_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": edited_default["snapshot_id"],
        "target": "function",
        "operation": "add",
        "name": "ResetSession",
        "signature": {"access": "public", "pure": False, "const": False, "parameters": []},
    })
    function_id = function.get("function", {}).get("id")
    local = bridge.call("blueprint_member_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": function["snapshot_id"],
        "target": "local_variable",
        "operation": "add",
        "function_id": function_id,
        "name": "PreviousRegion",
        "type": {"category": "string", "container": "none"},
        "default": {"kind": "literal", "value": ""},
    })
    inspection = bridge.call("asset_inspect", {"asset_path": asset_path})
    graph = bridge.call("asset_inspect", {
        "asset_path": asset_path, "selector": "event_graphs/EventGraph", "verbose": True,
    })
    event_graph_id = graph.get("graph", {}).get("debug", {}).get("graph_guid")
    if inspection.get("asset", {}).get("type") != "game_instance_blueprint" \
            or not isinstance(event_graph_id, str) or inspection.get("components"):
        raise AssertionError(f"GameInstance inspection contract mismatch: {inspection!r}")
    callback_catalog = bridge.call("blueprint_action_catalog", {
        "asset_path": asset_path,
        "graph_id": event_graph_id,
        "expected_snapshot": local["snapshot_id"],
        "node_family": "event",
        "owner_class": "/Script/Engine.GameInstance",
        "function": "ReceiveInit",
        "limit": 5,
    })
    if callback_catalog.get("blueprint_family") != "game_instance" or not callback_catalog.get("actions"):
        raise AssertionError(f"GameInstance Init callback is unavailable: {callback_catalog!r}")
    callback = bridge.call("blueprint_graph_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": local["snapshot_id"],
        "operation": "add_node",
        "graph_id": event_graph_id,
        "action_id": callback_catalog["actions"][0]["action_id"],
        "position": {"x": 160, "y": 120},
    })
    callback_node_id = callback.get("changed", {}).get("node", {}).get("id")
    if callback.get("blueprint_family") != "game_instance" or not isinstance(callback_node_id, str):
        raise AssertionError(f"GameInstance callback graph edit contract mismatch: {callback!r}")
    compiled = bridge.call("blueprint_compile", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": callback["snapshot_id"],
    })
    saved = bridge.call("blueprint_save", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": compiled["snapshot_id"],
    })
    if saved.get("compile_succeeded") is not True or saved.get("package_dirty") is not False:
        raise AssertionError(f"GameInstance compile/save contract mismatch: {saved!r}")
    return {
        "asset_path": asset_path,
        "snapshot_id": saved["snapshot_id"],
        "member_id": member.get("member", {}).get("id"),
        "function_id": function_id,
        "local_id": local.get("local_variable", {}).get("id"),
        "event_graph_id": event_graph_id,
        "callback_node_id": callback_node_id,
    }
