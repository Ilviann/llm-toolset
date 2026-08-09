"""Verify Blueprint fixtures after an editor restart."""

from __future__ import annotations

import json

from unreal_editor_mcp.bridge import UnrealBridge
from unreal_editor_mcp.project import ProjectLayout

from .pagination import collect_cursor_pages


def verify_restarted_blueprints(
    reloaded_bridge: UnrealBridge,
    layout: ProjectLayout,
    phase_two_loaded_snapshot: str,
    phase_two_loaded_inspection: dict[str, object],
    phase_fourteen_families: dict[str, dict[str, object]],
    phase_fifteen_game_instance: dict[str, object],
    scenario: dict[str, object],
) -> None:
    """Verify Blueprint identities, graphs, defaults, and catalogs after restart."""
    begin_play_node_id = scenario["begin_play_node_id"]
    conversion_node_id = scenario["conversion_node_id"]
    created_snapshot = scenario["created_snapshot"]
    custom_event_exec_source_node_id = scenario["custom_event_exec_source_node_id"]
    custom_event_exec_pin_id = scenario["custom_event_exec_pin_id"]
    custom_event_id = scenario["custom_event_id"]
    custom_replace_node_id = scenario["custom_replace_node_id"]
    function_id = scenario["function_id"]
    function_node_id = scenario["function_node_id"]
    graph_node_id = scenario["graph_node_id"]
    graph_pin_ids = scenario["graph_pin_ids"]
    literal_node_id = scenario["literal_node_id"]
    local_id = scenario["local_id"]
    macro_id = scenario["macro_id"]
    macro_replace_node_id = scenario["macro_replace_node_id"]
    member_id = scenario["member_id"]
    notify_id = scenario["notify_id"]
    operator_node_id = scenario["operator_node_id"]
    print_node_id = scenario["print_node_id"]
    setter_exec_pin_id = scenario["setter_exec_pin_id"]
    setter_node_id = scenario["setter_node_id"]
    setter_value_pin_id = scenario["setter_value_pin_id"]
    shared_print_node_id = scenario["shared_print_node_id"]
    temporary_node_id = scenario["temporary_node_id"]

    phase_two_reloaded = collect_inspection(reloaded_bridge, {
        "mode": "inspect",
        "asset_path": "/Game/UnrealMCPPhase2/BP_InspectionFixture.BP_InspectionFixture",
        "sections": ["summary", "parent_class", "compile_state", "components", "variables",
                     "graphs", "nodes", "pins", "connections"],
        "page_size": 100,
    })
    if phase_two_reloaded.get("snapshot_id") != phase_two_loaded_snapshot:
        before_records = {
            json.dumps(record, sort_keys=True, separators=(",", ":"))
            for record in phase_two_loaded_inspection.get("records", [])
        }
        after_records = {
            json.dumps(record, sort_keys=True, separators=(",", ":"))
            for record in phase_two_reloaded.get("records", [])
        }
        removed = sorted(before_records - after_records)[:8]
        added = sorted(after_records - before_records)[:8]
        raise AssertionError(
            "Phase 2 persisted structural snapshot changed between editor reloads: "
            f"expected {phase_two_loaded_snapshot}, received {phase_two_reloaded.get('snapshot_id')}; "
            f"removed={removed!r}; added={added!r}"
        )
    reloaded = collect_inspection(reloaded_bridge, {
        "mode": "inspect",
        "asset_path": "/Game/UnrealMCPPhase4/BP_ComponentFixture.BP_ComponentFixture",
        "sections": ["summary", "parent_class", "compile_state", "components", "class_defaults", "variables",
                     "functions", "macros", "custom_events", "parameters", "local_variables", "graphs", "nodes", "pins", "connections"],
        "property_names": ["InitialLifeSpan", "bReplicates", "bReplicateMovement"],
        "page_size": 100,
    })
    if reloaded.get("snapshot_id") != created_snapshot:
        raise AssertionError("created Blueprint snapshot changed after editor restart")
    for family, expected in phase_fourteen_families.items():
        family_reloaded = collect_inspection(reloaded_bridge, {
            "mode": "inspect",
            "asset_path": expected["asset_path"],
            "sections": ["summary", "components", "class_defaults", "functions", "local_variables", "graphs"],
            "property_names": [expected["property_name"]],
            "page_size": 100,
        })
        if family_reloaded.get("blueprint_family") != family \
                or family_reloaded.get("snapshot_id") != expected["snapshot_id"]:
            raise AssertionError(f"{family} identity changed after restart: {family_reloaded!r}")
        records = family_reloaded.get("records", [])
        if not any(record.get("section") == "component" and record.get("name") == "FamilyMovement"
                   for record in records):
            raise AssertionError(f"{family} component changed after restart")
        if not any(record.get("section") == "class_default"
                   and record.get("name") == expected["property_name"]
                   and record.get("value") == expected["property_value"] for record in records):
            raise AssertionError(f"{family} default changed after restart")
        if not any(record.get("section") == "function" and record.get("id") == expected["function_id"]
                   for record in records):
            raise AssertionError(f"{family} function changed after restart")
        if not any(record.get("section") == "local_variable" and record.get("id") == expected["local_id"]
                   for record in records):
            raise AssertionError(f"{family} local variable changed after restart")
        event_graphs = [record for record in records if record.get("section") == "graph"
                        and record.get("kind") == "event" and record.get("inherited") is False]
        catalog = reloaded_bridge.call("blueprint_action_catalog", {
            "asset_path": expected["asset_path"],
            "graph_id": event_graphs[0]["id"],
            "expected_snapshot": family_reloaded["snapshot_id"],
            "node_family": "function_call",
            "owner_class": expected["callable_owner"],
            "function": expected["callable_name"],
            "limit": 5,
        })
        if catalog.get("blueprint_family") != family or not catalog.get("actions"):
            raise AssertionError(f"{family} action changed after restart: {catalog!r}")
    game_instance_reloaded = collect_inspection(reloaded_bridge, {
        "mode": "inspect",
        "asset_path": phase_fifteen_game_instance["asset_path"],
        "sections": ["summary", "components", "class_defaults", "variables", "functions",
                     "local_variables", "graphs", "nodes"],
        "property_names": ["SessionRegion"],
        "page_size": 100,
    })
    if game_instance_reloaded.get("blueprint_family") != "game_instance" \
            or game_instance_reloaded.get("snapshot_id") != phase_fifteen_game_instance["snapshot_id"]:
        raise AssertionError(f"GameInstance identity changed after restart: {game_instance_reloaded!r}")
    game_instance_records = game_instance_reloaded.get("records", [])
    summaries = [record for record in game_instance_records if record.get("section") == "summary"]
    if len(summaries) != 1 or summaries[0].get("actor_blueprint") is not False \
            or summaries[0].get("family_capabilities", {}).get("components") is not False:
        raise AssertionError(f"GameInstance capabilities changed after restart: {summaries!r}")
    if any(record.get("section") == "component" for record in game_instance_records):
        raise AssertionError("GameInstance gained a component after restart")
    if not any(record.get("section") == "class_default" and record.get("name") == "SessionRegion"
               and record.get("value") == "eu-central" for record in game_instance_records):
        raise AssertionError("GameInstance session default changed after restart")
    if not any(record.get("section") == "variable" and record.get("id") == phase_fifteen_game_instance["member_id"]
               for record in game_instance_records):
        raise AssertionError("GameInstance member identity changed after restart")
    if not any(record.get("section") == "function" and record.get("id") == phase_fifteen_game_instance["function_id"]
               for record in game_instance_records):
        raise AssertionError("GameInstance function identity changed after restart")
    if not any(record.get("section") == "local_variable" and record.get("id") == phase_fifteen_game_instance["local_id"]
               for record in game_instance_records):
        raise AssertionError("GameInstance local identity changed after restart")
    if not any(record.get("section") == "node" and record.get("id") == phase_fifteen_game_instance["callback_node_id"]
               for record in game_instance_records):
        raise AssertionError("GameInstance Init callback changed after restart")
    shutdown_catalog = reloaded_bridge.call("blueprint_action_catalog", {
        "asset_path": phase_fifteen_game_instance["asset_path"],
        "graph_id": phase_fifteen_game_instance["event_graph_id"],
        "expected_snapshot": game_instance_reloaded["snapshot_id"],
        "node_family": "event",
        "owner_class": "/Script/Engine.GameInstance",
        "function": "ReceiveShutdown",
        "limit": 5,
    })
    if shutdown_catalog.get("blueprint_family") != "game_instance" or not shutdown_catalog.get("actions"):
        raise AssertionError(f"GameInstance Shutdown callback changed after restart: {shutdown_catalog!r}")
    parent_records = [record for record in reloaded.get("records", []) if record.get("section") == "parent_class"]
    if len(parent_records) != 1 or parent_records[0].get("class_path") != "/Script/Engine.Actor":
        raise AssertionError("created Blueprint parent changed after editor restart")
    compile_records = [record for record in reloaded.get("records", []) if record.get("section") == "compile_state"]
    if len(compile_records) != 1 or compile_records[0].get("state") not in {"up_to_date", "up_to_date_with_warnings"}:
        raise AssertionError("created Blueprint did not reload in a compiled state")
    components = {
        record.get("name"): record
        for record in reloaded.get("records", [])
        if record.get("section") == "component" and record.get("ownership") == "local"
    }
    if not {"SceneRoot", "Visual", "Movement"}.issubset(components):
        raise AssertionError(f"created component hierarchy changed after restart: {sorted(components)!r}")
    if components["SceneRoot"].get("root") is not True:
        raise AssertionError("saved scene root was not restored as root")
    if components["Visual"].get("parent_id") != components["SceneRoot"].get("id"):
        raise AssertionError("saved scene attachment changed after restart")
    defaults = [
        record for record in reloaded.get("records", [])
        if record.get("section") == "class_default" and record.get("name") == "InitialLifeSpan"
    ]
    if len(defaults) != 1 or defaults[0].get("value") != 12.5:
        raise AssertionError(f"edited Actor class default changed after restart: {defaults!r}")
    members = [
        record for record in reloaded.get("records", [])
        if record.get("section") == "variable" and record.get("name") == "Health"
    ]
    if len(members) != 1 or members[0].get("id") != member_id:
        raise AssertionError(f"member identity changed after restart: {members!r}")
    health = members[0]
    if health.get("type", {}).get("category") != "int" or health.get("default") != {"kind": "literal", "value": 100}:
        raise AssertionError(f"member type/default changed after restart: {health!r}")
    if health.get("metadata", {}).get("category") != "Stats" or health.get("metadata", {}).get("save_game") is not True:
        raise AssertionError(f"member metadata changed after restart: {health!r}")
    if health.get("replication", {}).get("mode") != "rep_notify" or health.get("replication", {}).get("relationship_valid") is not True:
        raise AssertionError(f"member replication changed after restart: {health!r}")
    functions = {
        record.get("name"): record
        for record in reloaded.get("records", [])
        if record.get("section") == "function"
    }
    if functions.get("ComputeHealth", {}).get("id") != function_id:
        raise AssertionError(f"function identity changed after restart: {functions!r}")
    function_boundary = functions.get("ComputeHealth", {}).get("replacement_boundary", {})
    if function_boundary.get("replaceable") is not True \
            or function_node_id not in function_boundary.get("owned_node_ids", []) \
            or not function_boundary.get("function_fingerprint"):
        raise AssertionError(f"function replacement boundary changed after restart: {function_boundary!r}")
    if functions.get("OnRep_Health", {}).get("id") != notify_id:
        raise AssertionError(f"RepNotify function identity changed after restart: {functions!r}")
    parameters = [
        record for record in reloaded.get("records", [])
        if record.get("section") == "parameter" and record.get("function_id") == function_id
    ]
    if [(item.get("name"), item.get("direction")) for item in parameters] != [("Delta", "input"), ("Result", "output")]:
        raise AssertionError(f"function signature changed after restart: {parameters!r}")
    locals_ = [
        record for record in reloaded.get("records", [])
        if record.get("section") == "local_variable" and record.get("id") == local_id
    ]
    if len(locals_) != 1 or locals_[0].get("scope", {}).get("function_id") != function_id:
        raise AssertionError(f"local-variable scope changed after restart: {locals_!r}")
    macros = [
        record for record in reloaded.get("records", [])
        if record.get("section") == "macro" and record.get("id") == macro_id
    ]
    if len(macros) != 1 or macros[0].get("name") != "ClampHealth" or macros[0].get("required_nodes", {}).get("valid") is not True:
        raise AssertionError(f"macro shell changed after restart: {macros!r}")
    if macros[0].get("signature", {}).get("pure") is not True:
        raise AssertionError(f"macro signature changed after restart: {macros!r}")
    macro_boundary = macros[0].get("replacement_boundary", {})
    if macro_replace_node_id not in macro_boundary.get("owned_node_ids", []) \
            or not macro_boundary.get("logic_unit_fingerprint"):
        raise AssertionError(f"macro replacement boundary changed after restart: {macro_boundary!r}")
    custom_events = [
        record for record in reloaded.get("records", [])
        if record.get("section") == "custom_event" and record.get("id") == custom_event_id
    ]
    if len(custom_events) != 1 or custom_events[0].get("name") != "OnHealthChanged":
        raise AssertionError(f"custom-event shell changed after restart: {custom_events!r}")
    if custom_events[0].get("graph_relationship", {}).get("graph_kind") != "event":
        raise AssertionError(f"custom-event graph relationship changed after restart: {custom_events!r}")
    if custom_events[0].get("metadata", {}).get("rpc_mode") != "server" \
            or custom_events[0].get("metadata", {}).get("reliability") != "reliable":
        raise AssertionError(f"custom-event RPC semantics changed after restart: {custom_events!r}")
    custom_boundary = custom_events[0].get("replacement_boundary", {})
    if custom_replace_node_id not in custom_boundary.get("owned_node_ids", []) \
            or not custom_boundary.get("logic_unit_fingerprint"):
        raise AssertionError(
            f"custom-event replacement boundary changed after restart: {custom_boundary!r}")
    nodes = {
        record.get("id"): record
        for record in reloaded.get("records", [])
        if record.get("section") == "node"
    }
    if graph_node_id not in nodes or nodes[graph_node_id].get("x") != 480 or nodes[graph_node_id].get("y") != -160:
        raise AssertionError(f"created/moved graph node changed after restart: {nodes.get(graph_node_id)!r}")
    if function_node_id not in nodes:
        raise AssertionError("replaced function body node changed after restart")
    if temporary_node_id in nodes:
        raise AssertionError("removed graph node returned after restart")
    reloaded_pin_ids = {
        record.get("id")
        for record in reloaded.get("records", [])
        if record.get("section") == "pin" and record.get("node_id") == graph_node_id
    }
    if set(graph_pin_ids) != reloaded_pin_ids:
        raise AssertionError(f"created pin identities changed after restart: {sorted(reloaded_pin_ids)!r}")
    setter_pins = {
        record.get("id"): record
        for record in reloaded.get("records", [])
        if record.get("section") == "pin" and record.get("node_id") == setter_node_id
    }
    if setter_value_pin_id not in setter_pins or setter_pins[setter_value_pin_id].get("default") != {"kind": "literal", "value": 77}:
        raise AssertionError(f"Phase 12 pin default changed after restart: {setter_pins.get(setter_value_pin_id)!r}")
    connections = [record for record in reloaded.get("records", []) if record.get("section") == "connection"]
    expected_connection = {
        "from_node_id": custom_event_exec_source_node_id,
        "from_pin_id": custom_event_exec_pin_id,
        "to_node_id": setter_node_id,
        "to_pin_id": setter_exec_pin_id,
    }
    if not any(all(record.get(key) == value for key, value in expected_connection.items()) for record in connections):
        raise AssertionError(f"Phase 12 direct connection changed after restart: {connections!r}")
    for node_id, label in {
        literal_node_id: "literal",
        operator_node_id: "specialized operator",
        print_node_id: "PrintString",
        shared_print_node_id: "shared PrintString",
        begin_play_node_id: "BeginPlay",
        conversion_node_id: "conversion",
    }.items():
        if node_id not in nodes:
            raise AssertionError(f"Phase 13 {label} node changed after restart: {node_id!r}")
    def has_connection(from_node_id: str, to_node_id: str) -> bool:
        return any(record.get("from_node_id") == from_node_id and record.get("to_node_id") == to_node_id
                   for record in connections)
    if not has_connection(literal_node_id, operator_node_id):
        raise AssertionError("Phase 13 wildcard-specialized input link changed after restart")
    if not has_connection(operator_node_id, conversion_node_id) \
            or not has_connection(conversion_node_id, print_node_id) \
            or not has_connection(conversion_node_id, shared_print_node_id):
        raise AssertionError("Phase 13 explicit conversion path changed after restart")
    if not has_connection(begin_play_node_id, print_node_id):
        raise AssertionError("Phase 13 BeginPlay behavior link changed after restart")
    begin_play_boundary = nodes[begin_play_node_id].get("replacement_boundary", {})
    if print_node_id not in begin_play_boundary.get("owned_node_ids", []) \
            or not begin_play_boundary.get("logic_unit_fingerprint") \
            or not any(link.get("from_node_id") == conversion_node_id
                       and link.get("to_node_id") == print_node_id
                       for link in begin_play_boundary.get("external_links", [])):
        raise AssertionError(
            f"native-event replacement boundary changed after restart: {begin_play_boundary!r}")
    event_graphs = [
        record for record in reloaded.get("records", [])
        if record.get("section") == "graph" and record.get("kind") == "event" and record.get("inherited") is False
    ]
    if not event_graphs:
        raise AssertionError("reloaded Blueprint has no local event graph for action catalog")
    catalog_base = {
        "asset_path": "/Game/UnrealMCPPhase4/BP_ComponentFixture.BP_ComponentFixture",
        "graph_id": event_graphs[0]["id"],
        "expected_snapshot": reloaded["snapshot_id"],
    }
    variable_actions = UnrealBridge(layout, timeout=3.0).call("blueprint_action_catalog", {
        **catalog_base, "member": "Health", "node_family": "variable_get", "limit": 5,
    })
    if not any(action.get("member_name") == "Health" for action in variable_actions.get("actions", [])):
        raise AssertionError(f"Phase 10 variable action missing after restart: {variable_actions!r}")
    function_actions = UnrealBridge(layout, timeout=3.0).call("blueprint_action_catalog", {
        **catalog_base, "function": "ComputeHealth", "node_family": "function_call", "limit": 5,
    })
    if not any(action.get("member_name") == "ComputeHealth" for action in function_actions.get("actions", [])):
        raise AssertionError(f"Phase 10 function action missing after restart: {function_actions!r}")
    expanded_queries = {
        "event": {"node_family": "event"},
        "flow_control": {"node_family": "flow_control"},
        "cast": {"node_family": "cast", "owner_class": "/Script/Engine.Actor"},
        "literal": {
            "node_family": "literal",
            "owner_class": "/Script/Engine.KismetSystemLibrary",
            "function": "MakeLiteralInt",
        },
        "operator": {
            "node_family": "operator",
            "owner_class": "/Script/Engine.KismetMathLibrary",
        },
    }
    for family, filters in expanded_queries.items():
        catalog = reloaded_bridge.call("blueprint_action_catalog", {
            **catalog_base, **filters, "limit": 10,
        })
        actions = catalog.get("actions", [])
        if not actions or any(action.get("node_family") != family for action in actions):
            raise AssertionError(f"Phase 10 {family} action missing after restart: {catalog!r}")
        if len(json.dumps(catalog, separators=(",", ":"))) > 32_768:
            raise AssertionError(f"Phase 10 {family} catalog exceeded representative context budget")
    operator_catalog = reloaded_bridge.call("blueprint_action_catalog", {
        **catalog_base,
        "node_family": "operator",
        "owner_class": "/Script/Engine.KismetMathLibrary",
        "limit": 50,
    })
    if not any(action.get("wildcard") is True for action in operator_catalog.get("actions", [])):
        raise AssertionError(f"Phase 10 wildcard operator action missing: {operator_catalog!r}")


def collect_inspection(bridge: UnrealBridge, arguments: dict[str, object]) -> dict[str, object]:
    """Consume one bounded inspection cursor chain without treating prose as a fixture."""
    result = bridge.call("blueprint_inspect", arguments)
    merged = dict(result)
    merged["records"] = collect_cursor_pages(
        result,
        lambda cursor: bridge.call("blueprint_inspect", {"cursor": cursor, "page_size": 100}),
    )
    merged.pop("next_cursor", None)
    return merged
