"""Author and validate the representative Blueprint graph-editing scenario."""

from __future__ import annotations

import os
import uuid

from unreal_editor_mcp.bridge import UnrealBridge
from unreal_editor_mcp.errors import BridgeError, ErrorCode
from unreal_editor_mcp.project import ProjectLayout

from .blueprint_declarations import author_blueprint_declarations
from .blueprint_restart_verification import collect_inspection


def author_blueprint_scenario(
    bridge: UnrealBridge,
    layout: ProjectLayout,
    capabilities: dict[str, object],
    created: dict[str, object],
    phase_fourteen_families: dict[str, dict[str, object]],
    phase_fifteen_game_instance: dict[str, object],
) -> dict[str, object]:
    """Author, compile, save, and validate the complete Blueprint graph fixture."""
    from .lifecycle import reconcile_operation, send_without_reading

    declarations = author_blueprint_declarations(
        bridge,
        layout,
        capabilities,
        created,
    )
    asset_path = declarations["asset_path"]
    custom_event = declarations["custom_event"]
    custom_event_id = declarations["custom_event_id"]
    event_graph_id = declarations["event_graph_id"]
    function_id = declarations["function_id"]
    local_id = declarations["local_id"]
    macro_id = declarations["macro_id"]
    member_id = declarations["member_id"]
    notify_id = declarations["notify_id"]

    function_inspection: dict[str, object] = {}
    boundary: dict[str, object] = {}
    replacement_catalog: dict[str, object] = {}
    for attempt in range(3):
        function_inspection = collect_inspection(bridge, {
            "mode": "inspect",
            "asset_path": asset_path,
            "sections": ["functions"],
            "page_size": 100,
        })
        function_records = [
            record for record in function_inspection.get("records", [])
            if record.get("section") == "function" and record.get("id") == function_id
        ]
        if len(function_records) != 1:
            raise AssertionError(
                f"function replacement boundary is unavailable: {function_inspection!r}")
        boundary = function_records[0].get("replacement_boundary", {})
        if boundary.get("replaceable") is not True:
            raise AssertionError(f"function replacement boundary is not editable: {boundary!r}")
        try:
            replacement_catalog = bridge.call("blueprint_action_catalog", {
                "asset_path": asset_path,
                "graph_id": function_id,
                "expected_snapshot": function_inspection["snapshot_id"],
                "text": "Branch",
                "node_family": "flow_control",
                "limit": 50,
            })
            break
        except BridgeError as error:
            if error.code != ErrorCode.STALE_PRECONDITION or attempt == 2:
                raise
    branch_actions = [
        action for action in replacement_catalog.get("actions", [])
        if str(action.get("title", "")).casefold() == "branch"
    ]
    if not branch_actions:
        raise AssertionError(f"function replacement Branch action is unavailable: {replacement_catalog!r}")
    function_replace_operation = uuid.uuid4().hex
    function_replace_arguments = {
        "operation_id": function_replace_operation,
        "asset_path": asset_path,
        "expected_snapshot": function_inspection["snapshot_id"],
        "function_id": function_id,
        "expected_function_fingerprint": boundary["function_fingerprint"],
        "entry_node_id": boundary["entry_node_id"],
        "result_node_id": boundary["result_node_id"],
        "owned_node_ids": boundary["owned_node_ids"],
        "local_variable_ids": boundary["local_variable_ids"],
        "entry_position": {"x": -320, "y": 0},
        "result_position": {"x": 640, "y": 0},
        "nodes": [{
            "key": "branch",
            "action_id": branch_actions[0]["action_id"],
            "position": {"x": 0, "y": 0},
        }],
        "pin_defaults": [{
            "endpoint": {"node_key": "$result", "pin_name": "Result"},
            "value": {"kind": "literal", "value": 7},
        }],
        "connections": [
            {
                "from": {"node_key": "$entry", "pin_name": "then"},
                "to": {"node_key": "branch", "pin_name": "execute"},
            },
            {
                "from": {"node_key": "branch", "pin_name": "then"},
                "to": {"node_key": "$result", "pin_name": "execute"},
            },
        ],
    }
    send_without_reading(layout, "blueprint_block_replace", function_replace_arguments)
    function_replace_status = reconcile_operation(
        bridge, function_replace_operation, capabilities["bridge_instance_id"])
    function_replace = (
        function_replace_status.get("result")
        if function_replace_status.get("state") == "committed" else None
    )
    function_replace_nodes = (
        function_replace.get("changed", {}).get("nodes", [])
        if isinstance(function_replace, dict) else []
    )
    if len(function_replace_nodes) != 1 or function_replace.get("changed", {}).get(
            "scratch_preflight") is not True:
        raise AssertionError(
            f"lost function-replacement response did not reconcile: {function_replace_status!r}")
    function_replace_replay = bridge.call(
        "blueprint_block_replace", function_replace_arguments)
    if function_replace_replay != function_replace:
        raise AssertionError("function replacement replay changed its retained terminal result")
    function_node_id = function_replace_nodes[0].get("id")
    if not isinstance(function_node_id, str) or len(function_node_id) != 32:
        raise AssertionError(f"function replacement omitted its node identity: {function_replace!r}")

    macro_inspection = collect_inspection(bridge, {
        "mode": "inspect", "asset_path": asset_path,
        "sections": ["macros"], "page_size": 100,
    })
    macro_records = [record for record in macro_inspection.get("records", [])
                     if record.get("section") == "macro" and record.get("id") == macro_id]
    if len(macro_records) != 1:
        raise AssertionError(f"macro replacement boundary is unavailable: {macro_inspection!r}")
    macro_boundary = macro_records[0].get("replacement_boundary", {})
    macro_catalog = bridge.call("blueprint_action_catalog", {
        "asset_path": asset_path,
        "graph_id": macro_id,
        "expected_snapshot": macro_inspection["snapshot_id"],
        "node_family": "literal",
        "owner_class": "/Script/Engine.KismetSystemLibrary",
        "function": "MakeLiteralInt",
        "limit": 5,
    })
    if not macro_catalog.get("actions"):
        raise AssertionError(f"macro replacement literal action is unavailable: {macro_catalog!r}")
    macro_replace = bridge.call("blueprint_block_replace", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": macro_inspection["snapshot_id"],
        "target_kind": "macro",
        "logic_unit_id": macro_id,
        "graph_id": macro_id,
        "expected_logic_unit_fingerprint": macro_boundary["logic_unit_fingerprint"],
        "entry_node_id": macro_boundary["entry_node_id"],
        "result_node_id": macro_boundary["result_node_id"],
        "owned_node_ids": macro_boundary["owned_node_ids"],
        "local_variable_ids": [],
        "entry_position": {"x": -320, "y": 0},
        "result_position": {"x": 640, "y": 0},
        "nodes": [{
            "key": "literal",
            "action_id": macro_catalog["actions"][0]["action_id"],
            "position": {"x": 0, "y": 0},
        }],
        "pin_defaults": [],
        "connections": [{
            "from": {"node_key": "literal", "pin_name": "ReturnValue"},
            "to": {"node_key": "$result", "pin_name": "Result"},
        }],
        "external_connections": [],
    })
    macro_replace_node_id = macro_replace.get("changed", {}).get("nodes", [{}])[0].get("id")
    if not isinstance(macro_replace_node_id, str):
        raise AssertionError(f"macro replacement omitted its created node: {macro_replace!r}")

    custom_inspection = collect_inspection(bridge, {
        "mode": "inspect", "asset_path": asset_path,
        "sections": ["custom_events"], "page_size": 100,
    })
    custom_records = [record for record in custom_inspection.get("records", [])
                      if record.get("section") == "custom_event"
                      and record.get("id") == custom_event_id]
    if len(custom_records) != 1:
        raise AssertionError(
            f"custom-event replacement boundary is unavailable: {custom_inspection!r}")
    custom_boundary = custom_records[0].get("replacement_boundary", {})
    custom_catalog = bridge.call("blueprint_action_catalog", {
        "asset_path": asset_path,
        "graph_id": event_graph_id,
        "expected_snapshot": custom_inspection["snapshot_id"],
        "text": "Branch",
        "node_family": "flow_control",
        "limit": 50,
    })
    custom_branch_actions = [action for action in custom_catalog.get("actions", [])
                             if str(action.get("title", "")).casefold() == "branch"]
    if not custom_branch_actions:
        raise AssertionError(
            f"custom-event replacement Branch action is unavailable: {custom_catalog!r}")
    custom_replace = bridge.call("blueprint_block_replace", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": custom_inspection["snapshot_id"],
        "target_kind": "custom_event",
        "logic_unit_id": custom_event_id,
        "graph_id": event_graph_id,
        "expected_logic_unit_fingerprint": custom_boundary["logic_unit_fingerprint"],
        "entry_node_id": custom_boundary["entry_node_id"],
        "owned_node_ids": custom_boundary["owned_node_ids"],
        "local_variable_ids": [],
        "entry_position": {"x": -320, "y": 240},
        "nodes": [{
            "key": "branch",
            "action_id": custom_branch_actions[0]["action_id"],
            "position": {"x": 0, "y": 240},
        }],
        "pin_defaults": [{
            "endpoint": {"node_key": "branch", "pin_name": "Condition"},
            "value": {"kind": "literal", "value": True},
        }],
        "connections": [{
            "from": {"node_key": "$entry", "pin_name": "then"},
            "to": {"node_key": "branch", "pin_name": "execute"},
        }],
        "external_connections": [],
    })
    custom_replace_node_id = custom_replace.get("changed", {}).get("nodes", [{}])[0].get("id")
    if not isinstance(custom_replace_node_id, str):
        raise AssertionError(f"custom-event replacement omitted its created node: {custom_replace!r}")

    graph_catalog = bridge.call("blueprint_action_catalog", {
        "asset_path": asset_path,
        "graph_id": event_graph_id,
        "expected_snapshot": custom_replace["snapshot_id"],
        "member": "Health",
        "node_family": "variable_get",
        "limit": 5,
    })
    if not graph_catalog.get("actions"):
        raise AssertionError(f"Phase 11 retained node action is unavailable: {graph_catalog!r}")
    graph_add_operation = uuid.uuid4().hex
    graph_add_arguments = {
        "operation_id": graph_add_operation,
        "asset_path": asset_path,
        "expected_snapshot": custom_replace["snapshot_id"],
        "operation": "add_node",
        "graph_id": event_graph_id,
        "action_id": graph_catalog["actions"][0]["action_id"],
        "position": {"x": 160, "y": 240},
    }
    send_without_reading(layout, "blueprint_graph_edit", graph_add_arguments)
    graph_add_status = reconcile_operation(bridge, graph_add_operation, capabilities["bridge_instance_id"])
    if graph_add_status.get("state") != "committed" or not isinstance(graph_add_status.get("result"), dict):
        raise AssertionError(f"lost node-add response did not reconcile: {graph_add_status!r}")
    graph_add = graph_add_status["result"]
    graph_node = graph_add.get("changed", {}).get("node", {})
    graph_node_id = graph_node.get("id")
    graph_pin_ids = [pin.get("id") for pin in graph_node.get("pins", [])]
    if (not isinstance(graph_node_id, str) or len(graph_node_id) != 32
            or not graph_pin_ids or any(not isinstance(pin_id, str) or len(pin_id) != 32 for pin_id in graph_pin_ids)):
        raise AssertionError(f"node add omitted persistent node or pin identities: {graph_add!r}")

    graph_move_operation = uuid.uuid4().hex
    graph_move_arguments = {
        "operation_id": graph_move_operation,
        "asset_path": asset_path,
        "expected_snapshot": graph_add["snapshot_id"],
        "operation": "move_node",
        "graph_id": event_graph_id,
        "node_id": graph_node_id,
        "position": {"x": 480, "y": -160},
    }
    send_without_reading(layout, "blueprint_graph_edit", graph_move_arguments)
    graph_move_status = reconcile_operation(bridge, graph_move_operation, capabilities["bridge_instance_id"])
    graph_move = graph_move_status.get("result") if graph_move_status.get("state") == "committed" else None
    if not isinstance(graph_move, dict) or graph_move.get("changed", {}).get("node", {}).get("x") != 480:
        raise AssertionError(f"lost node-move response did not reconcile: {graph_move_status!r}")

    removal_catalog = bridge.call("blueprint_action_catalog", {
        "asset_path": asset_path,
        "graph_id": event_graph_id,
        "expected_snapshot": graph_move["snapshot_id"],
        "member": "Health",
        "node_family": "variable_get",
        "limit": 5,
    })
    if not removal_catalog.get("actions"):
        raise AssertionError(f"Phase 11 removal fixture action is unavailable: {removal_catalog!r}")
    temporary_add_operation = uuid.uuid4().hex
    temporary_add_arguments = {
        "operation_id": temporary_add_operation,
        "asset_path": asset_path,
        "expected_snapshot": graph_move["snapshot_id"],
        "operation": "add_node",
        "graph_id": event_graph_id,
        "action_id": removal_catalog["actions"][0]["action_id"],
        "position": {"x": 640, "y": 320},
    }
    send_without_reading(layout, "blueprint_graph_edit", temporary_add_arguments)
    temporary_add_status = reconcile_operation(bridge, temporary_add_operation, capabilities["bridge_instance_id"])
    temporary_add = temporary_add_status.get("result") if temporary_add_status.get("state") == "committed" else None
    temporary_node_id = temporary_add.get("changed", {}).get("node", {}).get("id") if isinstance(temporary_add, dict) else None
    if not isinstance(temporary_node_id, str):
        raise AssertionError(f"temporary node add did not reconcile: {temporary_add_status!r}")
    graph_remove_operation = uuid.uuid4().hex
    graph_remove_arguments = {
        "operation_id": graph_remove_operation,
        "asset_path": asset_path,
        "expected_snapshot": temporary_add["snapshot_id"],
        "operation": "remove_node",
        "graph_id": event_graph_id,
        "node_id": temporary_node_id,
    }
    send_without_reading(layout, "blueprint_graph_edit", graph_remove_arguments)
    graph_remove_status = reconcile_operation(bridge, graph_remove_operation, capabilities["bridge_instance_id"])
    graph_remove = graph_remove_status.get("result") if graph_remove_status.get("state") == "committed" else None
    if not isinstance(graph_remove, dict) or graph_remove.get("changed", {}).get("node", {}).get("id") != temporary_node_id:
        raise AssertionError(f"lost node-remove response did not reconcile: {graph_remove_status!r}")

    setter_catalog = bridge.call("blueprint_action_catalog", {
        "asset_path": asset_path,
        "graph_id": event_graph_id,
        "expected_snapshot": graph_remove["snapshot_id"],
        "member": "Health",
        "node_family": "variable_set",
        "limit": 5,
    })
    if not setter_catalog.get("actions"):
        raise AssertionError(f"Phase 12 setter action is unavailable: {setter_catalog!r}")
    setter_add_operation = uuid.uuid4().hex
    setter_add_arguments = {
        "operation_id": setter_add_operation,
        "asset_path": asset_path,
        "expected_snapshot": graph_remove["snapshot_id"],
        "operation": "add_node",
        "graph_id": event_graph_id,
        "action_id": setter_catalog["actions"][0]["action_id"],
        "position": {"x": 800, "y": 240},
    }
    send_without_reading(layout, "blueprint_graph_edit", setter_add_arguments)
    setter_add_status = reconcile_operation(bridge, setter_add_operation, capabilities["bridge_instance_id"])
    setter_add = setter_add_status.get("result") if setter_add_status.get("state") == "committed" else None
    setter_node_id = setter_add.get("changed", {}).get("node", {}).get("id") if isinstance(setter_add, dict) else None
    if not isinstance(setter_node_id, str):
        raise AssertionError(f"Phase 12 setter node did not reconcile: {setter_add_status!r}")
    pin_inspection = bridge.call("blueprint_inspect", {
        "mode": "inspect",
        "asset_path": asset_path,
        "sections": ["pins"],
        "graph_id": event_graph_id,
        "page_size": 100,
    })
    pins = [record for record in pin_inspection.get("records", []) if record.get("section") == "pin"]
    def exact_pin(node_id: str, direction: str, category: str, name: str | None = None) -> str:
        matches = [record for record in pins
                   if record.get("node_id") == node_id and record.get("direction") == direction
                   and record.get("type", {}).get("category") == category
                   and (name is None or record.get("name") == name)]
        if len(matches) != 1 or not isinstance(matches[0].get("id"), str):
            raise AssertionError(f"expected one stable {node_id}/{direction}/{category}/{name} pin: {matches!r}")
        return matches[0]["id"]
    setter_exec_pin_id = exact_pin(setter_node_id, "input", "exec")
    setter_value_pin_id = exact_pin(setter_node_id, "input", "int", "Health")
    custom_event_exec_source_node_id = custom_replace_node_id
    custom_event_exec_pin_id = exact_pin(
        custom_event_exec_source_node_id, "output", "exec", "then")

    pin_default_operation = uuid.uuid4().hex
    pin_default_arguments = {
        "operation_id": pin_default_operation,
        "asset_path": asset_path,
        "expected_snapshot": setter_add["snapshot_id"],
        "operation": "set_pin_default",
        "graph_id": event_graph_id,
        "node_id": setter_node_id,
        "pin_id": setter_value_pin_id,
        "default": {"kind": "literal", "value": 77},
    }
    send_without_reading(layout, "blueprint_graph_edit", pin_default_arguments)
    pin_default_status = reconcile_operation(bridge, pin_default_operation, capabilities["bridge_instance_id"])
    pin_default = pin_default_status.get("result") if pin_default_status.get("state") == "committed" else None
    if not isinstance(pin_default, dict) or pin_default.get("changed", {}).get("default") != {"kind": "literal", "value": 77}:
        raise AssertionError(f"lost pin-default response did not reconcile: {pin_default_status!r}")

    connection_arguments = {
        "asset_path": asset_path,
        "operation": "connect_pins",
        "graph_id": event_graph_id,
        "from_node_id": custom_event_exec_source_node_id,
        "from_pin_id": custom_event_exec_pin_id,
        "to_node_id": setter_node_id,
        "to_pin_id": setter_exec_pin_id,
    }
    connect_operation = uuid.uuid4().hex
    send_without_reading(layout, "blueprint_graph_edit", {
        **connection_arguments, "operation_id": connect_operation,
        "expected_snapshot": pin_default["snapshot_id"],
    })
    connect_status = reconcile_operation(bridge, connect_operation, capabilities["bridge_instance_id"])
    connected = connect_status.get("result") if connect_status.get("state") == "committed" else None
    if not isinstance(connected, dict) or connected.get("changed", {}).get("connection", {}).get("connected") is not True:
        raise AssertionError(f"lost direct-connect response did not reconcile: {connect_status!r}")

    disconnect_operation = uuid.uuid4().hex
    send_without_reading(layout, "blueprint_graph_edit", {
        **connection_arguments, "operation": "disconnect_pins", "operation_id": disconnect_operation,
        "expected_snapshot": connected["snapshot_id"],
    })
    disconnect_status = reconcile_operation(bridge, disconnect_operation, capabilities["bridge_instance_id"])
    disconnected = disconnect_status.get("result") if disconnect_status.get("state") == "committed" else None
    if not isinstance(disconnected, dict) or disconnected.get("changed", {}).get("connection", {}).get("connected") is not False:
        raise AssertionError(f"lost direct-disconnect response did not reconcile: {disconnect_status!r}")

    reconnect_operation = uuid.uuid4().hex
    send_without_reading(layout, "blueprint_graph_edit", {
        **connection_arguments, "operation_id": reconnect_operation,
        "expected_snapshot": disconnected["snapshot_id"],
    })
    reconnect_status = reconcile_operation(bridge, reconnect_operation, capabilities["bridge_instance_id"])
    reconnected = reconnect_status.get("result") if reconnect_status.get("state") == "committed" else None
    if not isinstance(reconnected, dict) or reconnected.get("changed", {}).get("connection", {}).get("direct") is not True:
        raise AssertionError(f"lost direct-reconnect response did not reconcile: {reconnect_status!r}")

    def add_exact_action(snapshot: str, filters: dict[str, object], position: dict[str, int]) -> dict[str, object]:
        catalog = bridge.call("blueprint_action_catalog", {
            "asset_path": asset_path,
            "graph_id": event_graph_id,
            "expected_snapshot": snapshot,
            **filters,
            "limit": 50,
        })
        actions = catalog.get("actions", [])
        if not actions:
            raise AssertionError(f"Phase 13 action is unavailable for {filters!r}: {catalog!r}")
        return bridge.call("blueprint_graph_edit", {
            "operation_id": uuid.uuid4().hex,
            "asset_path": asset_path,
            "expected_snapshot": snapshot,
            "operation": "add_node",
            "graph_id": event_graph_id,
            "action_id": actions[0]["action_id"],
            "position": position,
        })

    literal_add = add_exact_action(reconnected["snapshot_id"], {
        "node_family": "literal",
        "owner_class": "/Script/Engine.KismetSystemLibrary",
        "function": "MakeLiteralInt",
    }, {"x": 1120, "y": 240})
    literal_node_id = literal_add.get("changed", {}).get("node", {}).get("id")
    literal_output_pin_id = next((
        pin.get("id") for pin in literal_add.get("changed", {}).get("node", {}).get("pins", [])
        if pin.get("direction") == "output" and pin.get("type", {}).get("category") == "int"
    ), None)
    if not isinstance(literal_node_id, str) or not isinstance(literal_output_pin_id, str):
        raise AssertionError(f"Phase 13 literal identities are unavailable: {literal_add!r}")

    operator_catalog = bridge.call("blueprint_action_catalog", {
        "asset_path": asset_path,
        "graph_id": event_graph_id,
        "expected_snapshot": literal_add["snapshot_id"],
        "node_family": "operator",
        "owner_class": "/Script/Engine.KismetMathLibrary",
        "pin_context": {"node_id": literal_node_id, "pin_id": literal_output_pin_id},
        "limit": 50,
    })
    wildcard_actions = [action for action in operator_catalog.get("actions", []) if action.get("wildcard") is True]
    wildcard_action = next((
        action for action in wildcard_actions
        if str(action.get("member_name", "")).casefold().startswith("add_")
        or str(action.get("title", "")).casefold().startswith("add")
    ), wildcard_actions[0] if wildcard_actions else None)
    if wildcard_action is None:
        raise AssertionError(f"Phase 13 context-valid wildcard operator is unavailable: {operator_catalog!r}")
    operator_add = bridge.call("blueprint_graph_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": literal_add["snapshot_id"],
        "operation": "add_node",
        "graph_id": event_graph_id,
        "action_id": wildcard_action["action_id"],
        "position": {"x": 1360, "y": 240},
    })
    operator_node_id = operator_add.get("changed", {}).get("node", {}).get("id")
    if not isinstance(operator_node_id, str):
        raise AssertionError(f"Phase 13 operator identity is unavailable: {operator_add!r}")

    print_add = add_exact_action(operator_add["snapshot_id"], {
        "node_family": "function_call",
        "owner_class": "/Script/Engine.KismetSystemLibrary",
        "function": "PrintString",
    }, {"x": 1840, "y": 240})
    print_node_id = print_add.get("changed", {}).get("node", {}).get("id")
    if not isinstance(print_node_id, str):
        raise AssertionError(f"Phase 13 PrintString identity is unavailable: {print_add!r}")

    existing_event_nodes = collect_inspection(bridge, {
        "mode": "inspect", "asset_path": asset_path, "sections": ["nodes"],
        "graph_id": event_graph_id, "page_size": 100,
    })
    begin_play_matches = [
        record for record in existing_event_nodes.get("records", [])
        if record.get("section") == "node"
        and record.get("class_path") == "/Script/BlueprintGraph.K2Node_Event"
        and "beginplay" in str(record.get("title", "")).replace(" ", "").casefold()
    ]
    if len(begin_play_matches) == 1:
        begin_play_node_id = begin_play_matches[0].get("id")
        begin_play_snapshot = print_add["snapshot_id"]
    else:
        begin_play_add = add_exact_action(print_add["snapshot_id"], {
            "node_family": "event",
            "owner_class": "/Script/Engine.Actor",
            "function": "ReceiveBeginPlay",
        }, {"x": 1120, "y": 0})
        begin_play_node_id = begin_play_add.get("changed", {}).get("node", {}).get("id")
        begin_play_snapshot = begin_play_add["snapshot_id"]
    if not isinstance(begin_play_node_id, str):
        raise AssertionError(f"Phase 13 BeginPlay identity is unavailable: {begin_play_matches!r}")

    phase13_pins = collect_inspection(bridge, {
        "mode": "inspect", "asset_path": asset_path, "sections": ["pins"],
        "graph_id": event_graph_id, "page_size": 100,
    })
    phase13_pin_records = [record for record in phase13_pins.get("records", []) if record.get("section") == "pin"]
    def phase13_pin(node_id: str, direction: str, category: str, name: str | None = None) -> str:
        matches = [record for record in phase13_pin_records
                   if record.get("node_id") == node_id and record.get("direction") == direction
                   and record.get("type", {}).get("category") == category
                   and (name is None or record.get("name") == name)]
        if len(matches) != 1 or not isinstance(matches[0].get("id"), str):
            raise AssertionError(f"Phase 13 pin is ambiguous: {node_id}/{direction}/{category}/{name}: {matches!r}")
        return matches[0]["id"]
    operator_input_pin_id = phase13_pin(operator_node_id, "input", "wildcard", "A")
    print_exec_pin_id = phase13_pin(print_node_id, "input", "exec")
    print_text_pin_id = phase13_pin(print_node_id, "input", "string", "InString")
    begin_play_exec_pin_id = phase13_pin(begin_play_node_id, "output", "exec")

    wildcard_operation = uuid.uuid4().hex
    send_without_reading(layout, "blueprint_graph_edit", {
        "operation_id": wildcard_operation,
        "asset_path": asset_path,
        "expected_snapshot": begin_play_snapshot,
        "operation": "connect_pins",
        "graph_id": event_graph_id,
        "from_node_id": literal_node_id,
        "from_pin_id": literal_output_pin_id,
        "to_node_id": operator_node_id,
        "to_pin_id": operator_input_pin_id,
    })
    wildcard_status = reconcile_operation(bridge, wildcard_operation, capabilities["bridge_instance_id"])
    wildcard_connected = wildcard_status.get("result") if wildcard_status.get("state") == "committed" else None
    wildcard_change = wildcard_connected.get("changed", {}).get("connection", {}) if isinstance(wildcard_connected, dict) else {}
    if (wildcard_change.get("wildcard_specialized") is not True
            or not wildcard_connected.get("reconstructed_identities")):
        raise AssertionError(f"lost wildcard-specialization response did not reconcile: {wildcard_status!r}")

    specialized_pins = collect_inspection(bridge, {
        "mode": "inspect", "asset_path": asset_path, "sections": ["pins"],
        "graph_id": event_graph_id, "page_size": 100,
    })
    specialized_records = [record for record in specialized_pins.get("records", []) if record.get("section") == "pin"]
    operator_output_matches = [record for record in specialized_records
                               if record.get("node_id") == operator_node_id
                               and record.get("direction") == "output"
                               and isinstance(record.get("id"), str)]
    if len(operator_output_matches) != 1 or not isinstance(operator_output_matches[0].get("id"), str):
        operator_records = [record for record in specialized_records if record.get("node_id") == operator_node_id]
        raise AssertionError(f"specialized operator output is unavailable: {operator_records!r}")
    operator_output_pin_id = operator_output_matches[0]["id"]

    conversion_operation = uuid.uuid4().hex
    send_without_reading(layout, "blueprint_graph_edit", {
        "operation_id": conversion_operation,
        "asset_path": asset_path,
        "expected_snapshot": wildcard_connected["snapshot_id"],
        "operation": "connect_pins",
        "graph_id": event_graph_id,
        "from_node_id": operator_node_id,
        "from_pin_id": operator_output_pin_id,
        "to_node_id": print_node_id,
        "to_pin_id": print_text_pin_id,
        "automatic_conversion": True,
    })
    conversion_status = reconcile_operation(bridge, conversion_operation, capabilities["bridge_instance_id"])
    converted = conversion_status.get("result") if conversion_status.get("state") == "committed" else None
    conversion_change = converted.get("changed", {}).get("connection", {}) if isinstance(converted, dict) else {}
    conversion_nodes = converted.get("changed", {}).get("nodes", []) if isinstance(converted, dict) else []
    if (conversion_change.get("automatic_conversion") is not True
            or conversion_change.get("conversion_node_count") != 1 or len(conversion_nodes) != 1
            or not converted.get("created_identities")):
        raise AssertionError(f"lost automatic-conversion response did not reconcile: {conversion_status!r}")
    conversion_node_id = conversion_nodes[0].get("id")
    if not isinstance(conversion_node_id, str):
        raise AssertionError(f"conversion node identity is unavailable: {converted!r}")

    shared_print_add = add_exact_action(converted["snapshot_id"], {
        "node_family": "function_call",
        "owner_class": "/Script/Engine.KismetSystemLibrary",
        "function": "PrintString",
    }, {"x": 1840, "y": 480})
    shared_print_node_id = shared_print_add.get("changed", {}).get("node", {}).get("id")
    if not isinstance(shared_print_node_id, str):
        raise AssertionError(f"shared PrintString identity is unavailable: {shared_print_add!r}")
    shared_pin_inspection = collect_inspection(bridge, {
        "mode": "inspect", "asset_path": asset_path, "sections": ["pins"],
        "graph_id": event_graph_id, "page_size": 100,
    })
    shared_pin_records = [record for record in shared_pin_inspection.get("records", [])
                          if record.get("section") == "pin"]
    conversion_output_matches = [record for record in shared_pin_records
                                 if record.get("node_id") == conversion_node_id
                                 and record.get("direction") == "output"
                                 and record.get("type", {}).get("category") == "string"]
    shared_text_matches = [record for record in shared_pin_records
                           if record.get("node_id") == shared_print_node_id
                           and record.get("direction") == "input"
                           and record.get("name") == "InString"]
    if len(conversion_output_matches) != 1 or len(shared_text_matches) != 1:
        raise AssertionError(
            f"shared conversion pins are unavailable: {conversion_output_matches!r}, "
            f"{shared_text_matches!r}")
    conversion_output_pin_id = conversion_output_matches[0]["id"]
    shared_data_connected = bridge.call("blueprint_graph_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": shared_print_add["snapshot_id"],
        "operation": "connect_pins",
        "graph_id": event_graph_id,
        "from_node_id": conversion_node_id,
        "from_pin_id": conversion_output_pin_id,
        "to_node_id": shared_print_node_id,
        "to_pin_id": shared_text_matches[0]["id"],
    })
    begin_play_connected = bridge.call("blueprint_graph_edit", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": shared_data_connected["snapshot_id"],
        "operation": "connect_pins",
        "graph_id": event_graph_id,
        "from_node_id": begin_play_node_id,
        "from_pin_id": begin_play_exec_pin_id,
        "to_node_id": print_node_id,
        "to_pin_id": print_exec_pin_id,
    })
    event_inspection = collect_inspection(bridge, {
        "mode": "inspect", "asset_path": asset_path, "sections": ["nodes", "pins"],
        "graph_id": event_graph_id, "page_size": 100,
    })
    event_records = [record for record in event_inspection.get("records", [])
                     if record.get("section") == "node"
                     and record.get("id") == begin_play_node_id]
    if len(event_records) != 1:
        raise AssertionError(f"native-event replacement boundary is unavailable: {event_inspection!r}")
    event_boundary = event_records[0].get("replacement_boundary", {})
    event_catalog = bridge.call("blueprint_action_catalog", {
        "asset_path": asset_path,
        "graph_id": event_graph_id,
        "expected_snapshot": begin_play_connected["snapshot_id"],
        "node_family": "function_call",
        "owner_class": "/Script/Engine.KismetSystemLibrary",
        "function": "PrintString",
        "limit": 5,
    })
    if not event_catalog.get("actions"):
        raise AssertionError(f"event replacement PrintString action is unavailable: {event_catalog!r}")
    event_replace = bridge.call("blueprint_block_replace", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": begin_play_connected["snapshot_id"],
        "target_kind": "event",
        "logic_unit_id": begin_play_node_id,
        "graph_id": event_graph_id,
        "expected_logic_unit_fingerprint": event_boundary["logic_unit_fingerprint"],
        "entry_node_id": event_boundary["entry_node_id"],
        "owned_node_ids": event_boundary["owned_node_ids"],
        "local_variable_ids": [],
        "entry_position": {"x": 1120, "y": 0},
        "nodes": [{
            "key": "print",
            "action_id": event_catalog["actions"][0]["action_id"],
            "position": {"x": 1840, "y": 240},
        }],
        "pin_defaults": [],
        "connections": [{
            "from": {"node_key": "$entry", "pin_name": "then"},
            "to": {"node_key": "print", "pin_name": "execute"},
        }],
        "external_connections": [{
            "from": {"node_id": conversion_node_id, "pin_id": conversion_output_pin_id},
            "to": {"node_key": "print", "pin_name": "InString"},
        }],
    })
    print_node_id = event_replace.get("changed", {}).get("nodes", [{}])[0].get("id")
    if not isinstance(print_node_id, str):
        raise AssertionError(f"event replacement omitted its created node: {event_replace!r}")
    compiled = bridge.call("blueprint_compile", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": event_replace["snapshot_id"],
    })
    if compiled.get("compile_succeeded") is not True or compiled.get("saved") is not False:
        raise AssertionError(f"explicit Blueprint compile contract mismatch: {compiled!r}")
    saved = bridge.call("blueprint_save", {
        "operation_id": uuid.uuid4().hex,
        "asset_path": asset_path,
        "expected_snapshot": compiled["snapshot_id"],
    })
    if saved.get("saved") is not True or saved.get("package_dirty") is not False:
        raise AssertionError(f"explicit Blueprint save contract mismatch: {saved!r}")
    created_snapshot = saved.get("snapshot_id")
    if not isinstance(created_snapshot, str) or len(created_snapshot) != 40:
        raise AssertionError("created Blueprint did not return a structural snapshot")
    if os.name == "posix" and layout.token_file.stat().st_mode & 0o077:
        raise AssertionError("bridge token permissions are broader than the owning user")
    assigned_game_mode_class = phase_fourteen_families["game_mode_base"]["asset_path"] + "_C"
    assigned_game_instance_class = phase_fifteen_game_instance["asset_path"] + "_C"
    bridge.call("gameplay_framework_edit", {
        "operation_id": uuid.uuid4().hex,
        "project_hash": capabilities["project_hash"],
        "setting": "default_game_mode",
        "class_path": assigned_game_mode_class,
        "expected_class": "/Script/Engine.GameModeBase",
    })
    bridge.call("gameplay_framework_edit", {
        "operation_id": uuid.uuid4().hex,
        "project_hash": capabilities["project_hash"],
        "setting": "default_game_instance",
        "class_path": assigned_game_instance_class,
        "expected_class": "/Script/Engine.GameInstance",
    })
    return {
        "assigned_game_instance_class": assigned_game_instance_class,
        "assigned_game_mode_class": assigned_game_mode_class,
        "begin_play_node_id": begin_play_node_id,
        "conversion_node_id": conversion_node_id,
        "created_snapshot": created_snapshot,
        "custom_event_exec_source_node_id": custom_event_exec_source_node_id,
        "custom_event_exec_pin_id": custom_event_exec_pin_id,
        "custom_event_id": custom_event_id,
        "function_id": function_id,
        "function_node_id": function_node_id,
        "graph_node_id": graph_node_id,
        "graph_pin_ids": graph_pin_ids,
        "literal_node_id": literal_node_id,
        "local_id": local_id,
        "macro_id": macro_id,
        "macro_replace_node_id": macro_replace_node_id,
        "member_id": member_id,
        "notify_id": notify_id,
        "operator_node_id": operator_node_id,
        "print_node_id": print_node_id,
        "custom_replace_node_id": custom_replace_node_id,
        "setter_exec_pin_id": setter_exec_pin_id,
        "setter_node_id": setter_node_id,
        "setter_value_pin_id": setter_value_pin_id,
        "shared_print_node_id": shared_print_node_id,
        "temporary_node_id": temporary_node_id,
    }
