extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")
const Limits := preload("command_limits.gd")

const NODE_REFERENCE_FIELDS := ["path", "handle"]
const OPERATION_TYPES := [
	"add_node", "instantiate_scene", "set_property", "remove_node",
	"rename_node", "reparent_node", "attach_script", "detach_script",
	"connect_signal", "disconnect_signal", "add_to_group", "remove_from_group",
]

var _editor_interface
var _undo_redo
var _project_paths: RefCounted
var _property_values: RefCounted
var _scene_state: Callable


func _init(
	editor_interface,
	undo_redo,
	project_paths: RefCounted,
	property_values: RefCounted,
	scene_state := Callable(),
) -> void:
	_editor_interface = editor_interface
	_undo_redo = undo_redo
	_project_paths = project_paths
	_property_values = property_values
	_scene_state = scene_state


func handlers() -> Dictionary:
	return {"scene_transaction": Callable(self, "transact")}


func transact(arguments: Dictionary) -> Dictionary:
	var root: Node = _editor_interface.get_edited_scene_root()
	if root == null:
		return _failure("No scene is open", ErrorEnvelope.NOT_FOUND)
	var saved_scene: PackedScene = null
	if not root.scene_file_path.is_empty():
		saved_scene = ResourceLoader.load(root.scene_file_path) as PackedScene
	if saved_scene != null and saved_scene.get_state().get_base_scene_state() != null:
		return _failure("Inherited scenes are not supported by scene transactions")
	var operations = arguments.get("operations")
	if not operations is Array or operations.is_empty() or operations.size() > Limits.MAX_TRANSACTION_OPERATIONS:
		return _failure("Operations must contain 1 to %d entries" % Limits.MAX_TRANSACTION_OPERATIONS)
	if JSON.stringify(arguments).to_utf8_buffer().size() > Limits.MAX_REQUEST_BYTES:
		return _failure("Transaction payload exceeds the request limit")
	var history_id: int = _undo_redo.get_object_history_id(root)
	var history = _undo_redo.get_history_undo_redo(history_id)
	var version_before := 0 if history == null else int(history.get_version())
	var precondition := _check_preconditions(arguments.get("preconditions", {}), root, version_before)
	if not precondition.ok:
		return precondition
	var staged := _stage(root, operations)
	if not staged.ok:
		return staged
	var plan: Array = staged.result.plan
	var expected: Dictionary = staged.result.expected
	var results: Array = staged.result.results
	var created_actual: Array = staged.result.created_actual
	var shadow: Node = staged.result.shadow
	var action_name := str(arguments.get("label", "MCP: scene transaction"))
	if action_name.is_empty() or action_name.length() > 128:
		_cleanup_staged(shadow, created_actual)
		return _failure("Transaction label must be 1 to 128 characters")
	var committed := _commit(root, plan, action_name)
	if not committed.ok:
		_cleanup_staged(shadow, created_actual)
		return committed
	shadow.free()
	var postcondition := _check_postconditions(root, expected, plan)
	if not postcondition.ok:
		var active_history = _undo_redo.get_history_undo_redo(history_id)
		if active_history != null and active_history.has_undo():
			active_history.undo()
		return _failure(
			"Transaction postcondition failed and the action was undone",
			ErrorEnvelope.TRANSACTION_FAILED,
			postcondition.error.get("details", {}),
		)
	var version_after := version_before
	history = _undo_redo.get_history_undo_redo(history_id)
	if history != null:
		version_after = int(history.get_version())
	_finalize_results(root, results)
	return ErrorEnvelope.success({
		"scene": root.scene_file_path,
		"undo_version_before": version_before,
		"undo_version_after": version_after,
		"operation_count": operations.size(),
		"results": _public_results(results),
		"scene_dirty": _scene_dirty(root, version_after != version_before),
	})


func _check_preconditions(value: Variant, root: Node, undo_version: int) -> Dictionary:
	if not value is Dictionary or value.size() > 2:
		return _failure("Preconditions must be an object with scene and undo_version")
	for key in value:
		if key not in ["scene", "undo_version"]:
			return _failure("Unknown transaction precondition: %s" % key)
	if value.has("scene"):
		var expected_scene = value.scene
		if not expected_scene is String or expected_scene != root.scene_file_path:
			return _failure(
				"Edited scene changed since inspection", ErrorEnvelope.STALE_SCENE,
				{"expected_scene": expected_scene, "scene": root.scene_file_path},
			)
	if value.has("undo_version"):
		var expected_version = value.undo_version
		if (
			(not expected_version is int and not expected_version is float)
			or float(expected_version) != floor(float(expected_version))
			or expected_version < -1
		):
			return _failure("undo_version precondition must be an integer of -1 or greater")
		expected_version = int(expected_version)
		if expected_version != undo_version:
			return _failure(
				"Edited scene changed since inspection", ErrorEnvelope.STALE_SCENE,
				{"expected_undo_version": expected_version, "undo_version": undo_version},
			)
	return ErrorEnvelope.success(true)


func _stage(root: Node, operations: Array) -> Dictionary:
	var snapshot := PackedScene.new()
	if snapshot.pack(root) != OK:
		return _failure("Could not pack the scene for transaction validation")
	var shadow := snapshot.instantiate(PackedScene.GEN_EDIT_STATE_INSTANCE) as Node
	if shadow == null:
		return _failure("Could not create a transaction validation snapshot")
	var context := {
		"root": root,
		"shadow": shadow,
		"mapped": {},
		"handles": {},
		"plan": [],
		"results": [],
		"created_actual": [],
		"created_ids": {},
		"removed_ids": {},
		"undo_bytes": JSON.stringify(operations).to_utf8_buffer().size(),
	}
	_map_existing_nodes(root, shadow, context.mapped)
	for index in operations.size():
		var operation = operations[index]
		if not operation is Dictionary or operation.size() > 16:
			return _stage_failure(context, "Operation %d must be a bounded object" % index)
		var kind = operation.get("op")
		if not kind is String or kind not in OPERATION_TYPES:
			return _stage_failure(context, "Operation %d has an unsupported op" % index)
		var applied := _stage_operation(operation, context)
		if not applied.ok:
			return _stage_failure(
				context, "Operation %d failed: %s" % [index, ErrorEnvelope.message(applied)],
				str(applied.error.get("code", ErrorEnvelope.INVALID_ARGUMENT)),
				{"operation": index, "cause": applied.error.get("details", {})},
			)
		context.results.append(applied.result)
		if context.created_actual.size() > Limits.MAX_TRANSACTION_CREATED_NODES:
			return _stage_failure(context, "Transaction creates too many nodes")
		if context.undo_bytes > Limits.MAX_TRANSACTION_UNDO_BYTES:
			return _stage_failure(context, "Transaction retained undo data exceeds the limit")
	var stats := _tree_stats(shadow)
	if not stats.ok:
		return _stage_failure(context, ErrorEnvelope.message(stats))
	var expected := {}
	for shadow_id in context.mapped:
		var shadow_node: Node = instance_from_id(int(shadow_id))
		if shadow_node == null:
			continue
		var actual_node: Node = context.mapped[shadow_id]
		if shadow_node == shadow or shadow.is_ancestor_of(shadow_node):
			expected[actual_node.get_instance_id()] = "." if shadow_node == shadow else str(shadow.get_path_to(shadow_node))
	for result in context.results:
		if result.has("_node"):
			var result_node: Node = result._node
			result["_expected_path"] = expected.get(result_node.get_instance_id(), "")
	return ErrorEnvelope.success({
		"plan": context.plan,
		"expected": expected,
		"results": context.results,
		"created_actual": context.created_actual,
		"shadow": shadow,
	})


func _stage_operation(operation: Dictionary, context: Dictionary) -> Dictionary:
	match operation.op:
		"add_node": return _stage_add(operation, context, false)
		"instantiate_scene": return _stage_add(operation, context, true)
		"set_property": return _stage_property(operation, context)
		"remove_node": return _stage_remove(operation, context)
		"rename_node": return _stage_rename(operation, context)
		"reparent_node": return _stage_reparent(operation, context)
		"attach_script": return _stage_script(operation, context, true)
		"detach_script": return _stage_script(operation, context, false)
		"connect_signal": return _stage_signal(operation, context, true)
		"disconnect_signal": return _stage_signal(operation, context, false)
		"add_to_group": return _stage_group(operation, context, true)
		"remove_from_group": return _stage_group(operation, context, false)
	return _failure("Unsupported operation")


func _stage_add(operation: Dictionary, context: Dictionary, instantiate: bool) -> Dictionary:
	var parent_found := _resolve_reference(operation.get("parent"), context)
	if not parent_found.ok: return parent_found
	var shadow_parent: Node = parent_found.result.shadow
	var actual_parent: Node = parent_found.result.actual
	var safe := _check_safe_node(actual_parent, context, true)
	if not safe.ok: return safe
	var checked_name := _checked_name(operation.get("name"))
	if not checked_name.ok: return checked_name
	if shadow_parent.has_node(NodePath(checked_name.result)):
		return _failure("Parent already has a child with that name")
	var shadow_node: Node
	var actual_node: Node
	var result_type := ""
	if instantiate:
		var checked: Dictionary = _project_paths.check(operation.get("scene"), false, PackedStringArray(["tscn", "scn"]))
		if not checked.ok: return checked
		if context.root.scene_file_path == checked.result:
			return _failure("Cannot instantiate the edited scene inside itself")
		var resource := ResourceLoader.load(checked.result)
		if not resource is PackedScene: return _failure("PackedScene not found")
		shadow_node = resource.instantiate(PackedScene.GEN_EDIT_STATE_INSTANCE)
		actual_node = resource.instantiate(PackedScene.GEN_EDIT_STATE_INSTANCE)
		if shadow_node == null or actual_node == null: return _failure("Could not instantiate scene")
		result_type = actual_node.get_class()
	else:
		var node_type = operation.get("type")
		if not node_type is String or node_type.is_empty() or node_type.length() > 128:
			return _failure("Node type must be a non-empty string up to 128 characters")
		if not ClassDB.class_exists(node_type) or not ClassDB.can_instantiate(node_type) or not ClassDB.is_parent_class(node_type, "Node"):
			return _failure("Type must be an instantiable built-in Node class")
		shadow_node = ClassDB.instantiate(node_type) as Node
		actual_node = ClassDB.instantiate(node_type) as Node
		if shadow_node == null or actual_node == null: return _failure("Could not instantiate node")
		result_type = node_type
	shadow_node.name = checked_name.result
	actual_node.name = checked_name.result
	shadow_parent.add_child(shadow_node)
	shadow_node.owner = context.shadow
	_map_existing_nodes(actual_node, shadow_node, context.mapped)
	context.created_actual.append(actual_node)
	context.created_ids[actual_node.get_instance_id()] = true
	context.plan.append({"kind": "add", "node": actual_node, "parent": actual_parent})
	var handle_result := _bind_handle(operation.get("handle"), shadow_node, actual_node, context)
	if not handle_result.ok: return handle_result
	return ErrorEnvelope.success({
		"op": operation.op, "_node": actual_node, "type": result_type,
		"name": checked_name.result, "handle": handle_result.result,
	})


func _stage_property(operation: Dictionary, context: Dictionary) -> Dictionary:
	var found := _resolve_reference(operation.get("target"), context)
	if not found.ok: return found
	var safe := _check_safe_node(found.result.actual, context)
	if not safe.ok: return safe
	var property_name = operation.get("property")
	if not property_name is String or property_name.is_empty() or property_name.length() > 128:
		return _failure("Property must be a non-empty string up to 128 characters")
	if not operation.has("value"): return _failure("Missing value")
	var shadow_node: Node = found.result.shadow
	var actual_node: Node = found.result.actual
	var property_info := _editable_property(shadow_node, property_name)
	if property_info.is_empty(): return _failure("Editable property not found")
	var previous = shadow_node.get(property_name)
	var shadow_value: Dictionary = _property_values.convert(
		operation.value, int(property_info.type), property_info, context.shadow,
		func(reference): return _resolve_codec_reference(reference, context, true),
	)
	if not shadow_value.ok: return shadow_value
	var actual_info := _editable_property(actual_node, property_name)
	if actual_info.is_empty(): actual_info = property_info
	var actual_value: Dictionary = _property_values.convert(
		operation.value, int(actual_info.type), actual_info, context.root,
		func(reference): return _resolve_codec_reference(reference, context, false),
	)
	if not actual_value.ok: return actual_value
	if _editable_property(actual_node, property_name).size() > 0:
		previous = actual_node.get(property_name)
	shadow_node.set(property_name, shadow_value.result)
	context.undo_bytes += JSON.stringify(_property_values.encode(previous, actual_info, context.root)).to_utf8_buffer().size()
	context.plan.append({"kind": "property", "node": actual_node, "property": property_name, "value": actual_value.result, "previous": previous})
	return ErrorEnvelope.success({
		"op": operation.op, "_node": actual_node, "property": property_name,
		"value": _property_values.encode(shadow_node.get(property_name), property_info, context.shadow),
	})


func _stage_remove(operation: Dictionary, context: Dictionary) -> Dictionary:
	var found := _resolve_reference(operation.get("target"), context)
	if not found.ok: return found
	var shadow_node: Node = found.result.shadow
	var actual_node: Node = found.result.actual
	if actual_node == context.root: return _failure("Cannot remove the edited scene root")
	var safe := _check_safe_node(actual_node, context)
	if not safe.ok: return safe
	var old_path := str(context.shadow.get_path_to(shadow_node))
	context.undo_bytes += _estimate_subtree_bytes(actual_node, context.root)
	var shadow_parent := shadow_node.get_parent()
	var actual_parent := actual_node.get_parent()
	if context.created_ids.has(actual_node.get_instance_id()):
		actual_parent = _mapped_actual(shadow_parent, context)
	var index := shadow_node.get_index()
	shadow_parent.remove_child(shadow_node)
	context.removed_ids[actual_node.get_instance_id()] = true
	context.plan.append({
		"kind": "remove", "node": actual_node, "parent": actual_parent,
		"index": index, "owner": actual_node.owner,
	})
	return ErrorEnvelope.success({"op": operation.op, "_node": actual_node, "path": old_path, "removed": true})


func _stage_rename(operation: Dictionary, context: Dictionary) -> Dictionary:
	var found := _resolve_reference(operation.get("target"), context)
	if not found.ok: return found
	var safe := _check_safe_node(found.result.actual, context)
	if not safe.ok: return safe
	var checked_name := _checked_name(operation.get("name"))
	if not checked_name.ok: return checked_name
	var shadow_node: Node = found.result.shadow
	var actual_node: Node = found.result.actual
	var parent := shadow_node.get_parent()
	if parent != null:
		for sibling in parent.get_children():
			if sibling != shadow_node and sibling.name == checked_name.result:
				return _failure("Parent already has a child with that name")
	var from_path := "." if shadow_node == context.shadow else str(context.shadow.get_path_to(shadow_node))
	var previous_name := str(actual_node.name)
	shadow_node.name = checked_name.result
	var to_path := "." if shadow_node == context.shadow else str(context.shadow.get_path_to(shadow_node))
	context.plan.append({"kind": "rename", "node": actual_node, "name": checked_name.result, "previous": previous_name})
	var handle_result := _bind_handle(operation.get("handle"), shadow_node, actual_node, context)
	if not handle_result.ok: return handle_result
	return ErrorEnvelope.success({"op": operation.op, "_node": actual_node, "from": from_path, "to": to_path, "handle": handle_result.result})


func _stage_reparent(operation: Dictionary, context: Dictionary) -> Dictionary:
	var found := _resolve_reference(operation.get("target"), context)
	if not found.ok: return found
	var parent_found := _resolve_reference(operation.get("parent"), context)
	if not parent_found.ok: return parent_found
	var shadow_node: Node = found.result.shadow
	var actual_node: Node = found.result.actual
	var shadow_parent: Node = parent_found.result.shadow
	var actual_parent: Node = parent_found.result.actual
	if actual_node == context.root: return _failure("Cannot reparent the edited scene root")
	var safe := _check_safe_node(actual_node, context)
	if not safe.ok: return safe
	safe = _check_safe_node(actual_parent, context, true)
	if not safe.ok: return safe
	if shadow_node == shadow_parent or shadow_node.is_ancestor_of(shadow_parent):
		return _failure("Cannot reparent a node below itself")
	var final_name := str(shadow_node.name)
	if operation.has("name"):
		var checked_name := _checked_name(operation.name)
		if not checked_name.ok: return checked_name
		final_name = checked_name.result
	for sibling in shadow_parent.get_children():
		if sibling != shadow_node and sibling.name == final_name:
			return _failure("Destination parent already has a child with that name")
	var from_path := str(context.shadow.get_path_to(shadow_node))
	var old_shadow_parent := shadow_node.get_parent()
	var old_actual_parent := actual_node.get_parent()
	if context.created_ids.has(actual_node.get_instance_id()): old_actual_parent = _mapped_actual(old_shadow_parent, context)
	var old_index := shadow_node.get_index()
	var old_name := str(actual_node.name)
	var shadow_owner := shadow_node.owner
	shadow_node.owner = null
	shadow_node.reparent(shadow_parent, false)
	shadow_node.owner = shadow_owner
	shadow_node.name = final_name
	var to_path := str(context.shadow.get_path_to(shadow_node))
	context.plan.append({
		"kind": "reparent", "node": actual_node, "parent": actual_parent,
		"name": final_name, "previous_parent": old_actual_parent,
		"previous_index": old_index, "previous_name": old_name, "owner": context.root,
	})
	var handle_result := _bind_handle(operation.get("handle"), shadow_node, actual_node, context)
	if not handle_result.ok: return handle_result
	return ErrorEnvelope.success({"op": operation.op, "_node": actual_node, "from": from_path, "to": to_path, "handle": handle_result.result})


func _stage_script(operation: Dictionary, context: Dictionary, attach: bool) -> Dictionary:
	var found := _resolve_reference(operation.get("target"), context)
	if not found.ok: return found
	var safe := _check_safe_node(found.result.actual, context)
	if not safe.ok: return safe
	var script: Script = null
	var script_path := ""
	if attach:
		var checked: Dictionary = _project_paths.check(operation.get("script"), false, PackedStringArray(["gd", "cs"]))
		if not checked.ok: return checked
		var loaded := ResourceLoader.load(checked.result)
		if not loaded is Script: return _failure("Script resource not found")
		script = loaded
		script_path = checked.result
		var base_type := script.get_instance_base_type()
		if not base_type.is_empty() and not found.result.shadow.is_class(base_type):
			return _failure("Script base type is not compatible with the node")
	var shadow_node: Node = found.result.shadow
	var actual_node: Node = found.result.actual
	var previous = actual_node.get_script()
	shadow_node.set_script(script)
	context.plan.append({"kind": "script", "node": actual_node, "script": script, "previous": previous})
	return ErrorEnvelope.success({"op": operation.op, "_node": actual_node, "script": script_path if attach else null})


func _stage_signal(operation: Dictionary, context: Dictionary, connect_signal: bool) -> Dictionary:
	var source_found := _resolve_reference(operation.get("source"), context)
	if not source_found.ok: return source_found
	var target_found := _resolve_reference(operation.get("target"), context)
	if not target_found.ok: return target_found
	var source: Node = source_found.result.shadow
	var target: Node = target_found.result.shadow
	var actual_source: Node = source_found.result.actual
	var actual_target: Node = target_found.result.actual
	var safe := _check_safe_node(actual_source, context)
	if not safe.ok: return safe
	safe = _check_safe_node(actual_target, context)
	if not safe.ok: return safe
	var signal_name = operation.get("signal")
	var method = operation.get("method")
	if not signal_name is String or signal_name.is_empty() or signal_name.length() > 128 or not source.has_signal(signal_name):
		return _failure("Signal is not declared by the source node")
	if not method is String or method.is_empty() or method.length() > 128 or not target.has_method(method):
		return _failure("Signal target method is not declared")
	var callable := Callable(target, method)
	var already := source.is_connected(signal_name, callable)
	if connect_signal and already: return _failure("Signal connection already exists")
	if not connect_signal and not already: return _failure("Signal connection does not exist")
	if connect_signal:
		var error := source.connect(signal_name, callable, Object.CONNECT_PERSIST)
		if error != OK: return _failure("Signal connection could not be validated")
	else:
		source.disconnect(signal_name, callable)
	context.plan.append({"kind": "signal", "source": actual_source, "target": actual_target, "signal": signal_name, "method": method, "connect": connect_signal})
	return ErrorEnvelope.success({"op": operation.op, "source": _shadow_path(source, context), "target": _shadow_path(target, context), "signal": signal_name, "method": method})


func _stage_group(operation: Dictionary, context: Dictionary, add: bool) -> Dictionary:
	var found := _resolve_reference(operation.get("target"), context)
	if not found.ok: return found
	var safe := _check_safe_node(found.result.actual, context)
	if not safe.ok: return safe
	var group = operation.get("group")
	if not group is String or group.is_empty() or group.length() > 128:
		return _failure("Group must be a non-empty string up to 128 characters")
	var shadow_node: Node = found.result.shadow
	if add and shadow_node.is_in_group(group): return _failure("Node is already in the group")
	if not add and not shadow_node.is_in_group(group): return _failure("Node is not in the group")
	if add: shadow_node.add_to_group(group, true)
	else: shadow_node.remove_from_group(group)
	context.plan.append({"kind": "group", "node": found.result.actual, "group": group, "add": add})
	return ErrorEnvelope.success({"op": operation.op, "_node": found.result.actual, "group": group})


func _commit(root: Node, plan: Array, action_name: String) -> Dictionary:
	_undo_redo.create_action(action_name, UndoRedo.MERGE_DISABLE, root)
	for item in plan:
		match item.kind:
			"add":
				_undo_redo.add_do_method(item.parent, "add_child", item.node)
				_undo_redo.add_do_method(item.node, "set_owner", root)
				_undo_redo.add_do_reference(item.node)
				_undo_redo.add_undo_method(self, "_remove_owned_node", item.node, item.parent)
			"property":
				_undo_redo.add_do_property(item.node, item.property, item.value)
				_undo_redo.add_undo_property(item.node, item.property, item.previous)
			"remove":
				_undo_redo.add_do_method(self, "_remove_owned_node", item.node, item.parent)
				_undo_redo.add_undo_method(self, "_restore_node", item.node, item.parent, item.index, item.owner)
				_undo_redo.add_undo_reference(item.node)
			"rename":
				_undo_redo.add_do_property(item.node, "name", item.name)
				_undo_redo.add_undo_property(item.node, "name", item.previous)
			"reparent":
				_undo_redo.add_do_method(self, "_move_node", item.node, item.parent, -1, item.name, item.owner)
				_undo_redo.add_undo_method(self, "_move_node", item.node, item.previous_parent, item.previous_index, item.previous_name, item.owner)
			"script":
				_undo_redo.add_do_method(item.node, "set_script", item.script)
				_undo_redo.add_undo_method(item.node, "set_script", item.previous)
			"signal":
				_undo_redo.add_do_method(self, "_set_signal", item.source, item.signal, item.target, item.method, item.connect)
				_undo_redo.add_undo_method(self, "_set_signal", item.source, item.signal, item.target, item.method, not item.connect)
			"group":
				_undo_redo.add_do_method(self, "_set_group", item.node, item.group, item.add)
				_undo_redo.add_undo_method(self, "_set_group", item.node, item.group, not item.add)
	_undo_redo.commit_action()
	return ErrorEnvelope.success(true)


func _check_postconditions(root: Node, expected: Dictionary, plan: Array) -> Dictionary:
	for instance_id in expected:
		var node := instance_from_id(int(instance_id)) as Node
		if node == null:
			return _failure("A transaction node was freed", ErrorEnvelope.TRANSACTION_FAILED, {"instance_id": instance_id})
		var path := "." if node == root else (str(root.get_path_to(node)) if root.is_ancestor_of(node) else "")
		if path != expected[instance_id]:
			return _failure("A transaction node reached an unexpected path", ErrorEnvelope.TRANSACTION_FAILED, {"expected": expected[instance_id], "path": path})
	var checked_states := {}
	for plan_index in range(plan.size() - 1, -1, -1):
		var item: Dictionary = plan[plan_index]
		match item.kind:
			"property":
				var property_key := "%s:%s" % [item.node.get_instance_id(), item.property]
				if checked_states.has(property_key): continue
				checked_states[property_key] = true
				if item.node.get(item.property) != item.value:
					return _failure("A property postcondition did not match", ErrorEnvelope.TRANSACTION_FAILED, {"property": item.property})
			"script":
				var script_key := "%s:script" % item.node.get_instance_id()
				if checked_states.has(script_key): continue
				checked_states[script_key] = true
				if item.node.get_script() != item.script:
					return _failure("A script postcondition did not match", ErrorEnvelope.TRANSACTION_FAILED)
			"signal":
				var signal_key := "%s:%s:%s:%s" % [item.source.get_instance_id(), item.signal, item.target.get_instance_id(), item.method]
				if checked_states.has(signal_key): continue
				checked_states[signal_key] = true
				var connected: bool = item.source.is_connected(item.signal, Callable(item.target, item.method))
				if connected != item.connect:
					return _failure("A signal postcondition did not match", ErrorEnvelope.TRANSACTION_FAILED, {"signal": item.signal})
			"group":
				var group_key := "%s:group:%s" % [item.node.get_instance_id(), item.group]
				if checked_states.has(group_key): continue
				checked_states[group_key] = true
				if item.node.is_in_group(item.group) != item.add:
					return _failure("A group postcondition did not match", ErrorEnvelope.TRANSACTION_FAILED, {"group": item.group})
	return ErrorEnvelope.success(true)


func _resolve_reference(reference: Variant, context: Dictionary) -> Dictionary:
	if not reference is Dictionary or reference.size() != 1:
		return _failure("Node reference must contain exactly one path or handle")
	var key = reference.keys()[0]
	if key not in NODE_REFERENCE_FIELDS:
		return _failure("Node reference must contain path or handle")
	var value = reference[key]
	if not value is String or value.is_empty() or value.length() > 512:
		return _failure("Node reference value is invalid")
	var shadow_node: Node
	var actual_node: Node
	if key == "handle":
		if not context.handles.has(value): return _failure("Transaction handle not found")
		shadow_node = context.handles[value].shadow
		actual_node = context.handles[value].actual
	else:
		if value.begins_with("/") or ".." in value.split("/"):
			return _failure("Node reference path must be scene-relative")
		shadow_node = context.shadow if value == "." else context.shadow.get_node_or_null(NodePath(value))
		if shadow_node == null: return _failure("Node reference target not found", ErrorEnvelope.NOT_FOUND)
		actual_node = _mapped_actual(shadow_node, context)
	if shadow_node == null or actual_node == null or (shadow_node != context.shadow and not context.shadow.is_ancestor_of(shadow_node)):
		return _failure("Node reference target is no longer in the scene", ErrorEnvelope.NOT_FOUND)
	return ErrorEnvelope.success({"shadow": shadow_node, "actual": actual_node})


func _resolve_codec_reference(reference: Dictionary, context: Dictionary, shadow: bool) -> Dictionary:
	var found := _resolve_reference(reference, context)
	if not found.ok: return found
	return ErrorEnvelope.success(found.result.shadow if shadow else found.result.actual)


func _bind_handle(value: Variant, shadow_node: Node, actual_node: Node, context: Dictionary) -> Dictionary:
	if value == null: return ErrorEnvelope.success(null)
	if not value is String or value.is_empty() or value.length() > 64 or not value.is_valid_identifier():
		return _failure("Transaction handle must be a unique identifier up to 64 characters")
	if context.handles.has(value): return _failure("Transaction handle is already defined")
	context.handles[value] = {"shadow": shadow_node, "actual": actual_node}
	return ErrorEnvelope.success(value)


func _check_safe_node(node: Node, context: Dictionary, as_parent := false) -> Dictionary:
	if node == context.root: return ErrorEnvelope.success(true)
	if context.created_ids.has(node.get_instance_id()): return ErrorEnvelope.success(true)
	if node.owner != context.root:
		return _failure("Editable children of instantiated scenes are not supported")
	if as_parent and node.scene_file_path != "" and node.owner != context.root:
		return _failure("Cannot add local children below this instantiated scene node")
	return ErrorEnvelope.success(true)


func _map_existing_nodes(actual: Node, shadow: Node, mapped: Dictionary) -> void:
	mapped[shadow.get_instance_id()] = actual
	var actual_children := actual.get_children()
	var shadow_children := shadow.get_children()
	for index in mini(actual_children.size(), shadow_children.size()):
		_map_existing_nodes(actual_children[index], shadow_children[index], mapped)


func _mapped_actual(shadow_node: Node, context: Dictionary) -> Node:
	return context.mapped.get(shadow_node.get_instance_id()) as Node


func _editable_property(node: Node, property_name: String) -> Dictionary:
	for info in node.get_property_list():
		if str(info.name) == property_name and (int(info.usage) & PROPERTY_USAGE_EDITOR) != 0:
			return info
	return {}


func _checked_name(value: Variant) -> Dictionary:
	if not value is String or value.is_empty() or value.length() > 128:
		return _failure("Node name must be a non-empty string up to 128 characters")
	if value.validate_node_name() != value:
		return _failure("Node name contains invalid characters")
	return ErrorEnvelope.success(value)


func _tree_stats(root: Node) -> Dictionary:
	var queue: Array = [{"node": root, "depth": 0}]
	var scanned := 0
	while not queue.is_empty():
		var item: Dictionary = queue.pop_front()
		scanned += 1
		if scanned > Limits.MAX_TREE_SCAN: return _failure("Transaction scene scan exceeds the node limit")
		if item.depth > Limits.MAX_TRANSACTION_TREE_DEPTH: return _failure("Transaction tree depth exceeds the limit")
		for child in item.node.get_children(): queue.append({"node": child, "depth": item.depth + 1})
	return ErrorEnvelope.success({"nodes": scanned})


func _estimate_subtree_bytes(node: Node, scene_root: Node) -> int:
	var total := 0
	var queue: Array = [node]
	var scanned := 0
	while not queue.is_empty() and total <= Limits.MAX_TRANSACTION_UNDO_BYTES:
		var current: Node = queue.pop_front()
		scanned += 1
		if scanned > Limits.MAX_TREE_SCAN: return Limits.MAX_TRANSACTION_UNDO_BYTES + 1
		total += 128 + str(current.name).length()
		for info in current.get_property_list():
			if (int(info.usage) & PROPERTY_USAGE_STORAGE) != 0:
				total += JSON.stringify(_property_values.encode(current.get(info.name), info, scene_root)).to_utf8_buffer().size()
		for child in current.get_children(): queue.append(child)
	return total


func _cleanup_staged(shadow: Node, created_actual: Array) -> void:
	if shadow != null: shadow.free()
	for node in created_actual:
		if is_instance_valid(node) and node.get_parent() == null: node.free()


func _stage_failure(context: Dictionary, message: String, code := ErrorEnvelope.INVALID_ARGUMENT, details := {}) -> Dictionary:
	_cleanup_staged(context.shadow, context.created_actual)
	return _failure(message, code, details)


func _remove_owned_node(node: Node, parent: Node) -> void:
	node.owner = null
	parent.remove_child(node)


func _restore_node(node: Node, parent: Node, index: int, owner: Node) -> void:
	parent.add_child(node)
	if index >= 0 and index < parent.get_child_count(): parent.move_child(node, index)
	node.owner = owner


func _move_node(node: Node, parent: Node, index: int, node_name: String, owner: Node) -> void:
	node.owner = null
	node.reparent(parent, false)
	node.name = node_name
	node.owner = owner
	if index >= 0 and index < parent.get_child_count(): parent.move_child(node, index)


func _set_signal(source: Node, signal_name: String, target: Node, method: String, connect_value: bool) -> void:
	var callable := Callable(target, method)
	if connect_value and not source.is_connected(signal_name, callable): source.connect(signal_name, callable, Object.CONNECT_PERSIST)
	elif not connect_value and source.is_connected(signal_name, callable): source.disconnect(signal_name, callable)


func _set_group(node: Node, group: String, add: bool) -> void:
	if add and not node.is_in_group(group): node.add_to_group(group, true)
	elif not add and node.is_in_group(group): node.remove_from_group(group)


func _finalize_results(root: Node, results: Array) -> void:
	for result in results:
		if not result.has("_node"): continue
		var node: Node = result._node
		var expected_path := str(result.get("_expected_path", ""))
		if expected_path.is_empty(): result["removed"] = true
		else: result["path"] = expected_path


func _public_results(results: Array) -> Array:
	var output: Array = []
	for result in results:
		var public := {}
		for key in result:
			if not str(key).begins_with("_"): public[key] = result[key]
		output.append(public)
	return output


func _scene_dirty(root: Node, fallback: bool) -> bool:
	if _scene_state.is_valid():
		var state = _scene_state.call()
		if state is Dictionary and state.get("scene") == root.scene_file_path:
			return bool(state.get("scene_dirty", fallback))
	return fallback


func _shadow_path(node: Node, context: Dictionary) -> String:
	return "." if node == context.shadow else str(context.shadow.get_path_to(node))


func _failure(message: String, code := ErrorEnvelope.INVALID_ARGUMENT, details := {}) -> Dictionary:
	return ErrorEnvelope.failure(message, code, details, false)
