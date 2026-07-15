extends SceneTree

const Codec := preload("res://addons/godot_mcp/property_value_codec.gd")
const Limits := preload("res://addons/godot_mcp/command_limits.gd")
const Transaction := preload("res://addons/godot_mcp/scene_transaction.gd")


class FakeEditor extends RefCounted:
	var root: Node
	func _init(value: Node) -> void: root = value
	func get_edited_scene_root() -> Node: return root


class FakeProjectPaths extends RefCounted:
	func check(_value: Variant, _write := false, _extensions := PackedStringArray()) -> Dictionary:
		return {"ok": false, "error": {"message": "unused"}}


class FakeUndoRedo extends RefCounted:
	var actions: Array = []
	var current_do: Array = []
	var current_undo: Array = []
	var position := -1
	var version := -1
	var skip_properties := false

	func get_object_history_id(_object: Object) -> int: return 1
	func get_history_undo_redo(_history_id: int): return self
	func get_version() -> int: return version
	func has_undo() -> bool: return position >= 0

	func create_action(_name: String, _merge := 0, _context: Object = null) -> void:
		current_do = []
		current_undo = []

	func add_do_method(object: Object, method: String, a = null, b = null, c = null, d = null, e = null) -> void:
		current_do.append(_method_call(object, method, a, b, c, d, e))

	func add_undo_method(object: Object, method: String, a = null, b = null, c = null, d = null, e = null) -> void:
		current_undo.append(_method_call(object, method, a, b, c, d, e))

	func add_do_property(object: Object, property: String, value: Variant) -> void:
		current_do.append({"kind": "property", "object": object, "property": property, "value": value})

	func add_undo_property(object: Object, property: String, value: Variant) -> void:
		current_undo.append({"kind": "property", "object": object, "property": property, "value": value})

	func add_do_reference(_object: Object) -> void: pass
	func add_undo_reference(_object: Object) -> void: pass

	func commit_action() -> void:
		for call in current_do: _run(call)
		actions = actions.slice(0, position + 1)
		actions.append({"do": current_do.duplicate(), "undo": current_undo.duplicate()})
		position += 1
		version += 1

	func undo() -> void:
		for index in range(actions[position].undo.size() - 1, -1, -1):
			_run(actions[position].undo[index])
		position -= 1
		version -= 1

	func redo() -> void:
		position += 1
		for call in actions[position].do: _run(call)
		version += 1

	func _method_call(object: Object, method: String, a: Variant, b: Variant, c: Variant, d: Variant, e: Variant) -> Dictionary:
		var arguments: Array = [a, b, c, d, e]
		while not arguments.is_empty() and arguments.back() == null: arguments.pop_back()
		return {"kind": "method", "object": object, "method": method, "arguments": arguments}

	func _run(call: Dictionary) -> void:
		if call.kind == "property":
			if not skip_properties: call.object.set(call.property, call.value)
		else:
			call.object.callv(call.method, call.arguments)


func _init() -> void:
	var codec = Codec.new()
	var root := Node2D.new()
	root.name = "Root"
	var child := Node2D.new()
	child.name = "Child"
	root.add_child(child)

	var node_reference := codec.convert(
		{"$type": "node", "path": "Child"}, TYPE_OBJECT,
		{"hint": PROPERTY_HINT_NODE_TYPE, "hint_string": "Node2D"}, root,
	)
	_check(node_reference.ok and node_reference.result == child, "tagged node reference failed")
	var wrong_node_class := codec.convert(
		{"$type": "node", "path": "Child"}, TYPE_OBJECT,
		{"hint": PROPERTY_HINT_NODE_TYPE, "hint_string": "Control"}, root,
	)
	_check(not wrong_node_class.ok, "node class mismatch was accepted")
	var node_path := codec.convert({"$type": "node_path", "path": "Child"}, TYPE_NODE_PATH, {}, root)
	_check(node_path.ok and node_path.result == NodePath("Child"), "tagged NodePath failed")
	_check(not codec.convert({"$type": "node_path", "path": "../Other"}, TYPE_NODE_PATH, {}, root).ok, "escaping NodePath was accepted")

	var rect := codec.convert(
		{"$type": "rect2", "position": [1, 2], "size": [3, 4]}, TYPE_RECT2,
	)
	_check(rect.ok and rect.result == Rect2(1, 2, 3, 4), "Rect2 conversion failed")
	var transform := codec.convert({
		"$type": "transform3d",
		"basis": [[1, 0, 0], [0, 1, 0], [0, 0, 1]],
		"origin": [4, 5, 6],
	}, TYPE_TRANSFORM3D)
	_check(transform.ok and transform.result.origin == Vector3(4, 5, 6), "Transform3D conversion failed")
	var packed := codec.convert(
		{"$type": "packed_vector2_array", "values": [[1, 2], [3, 4]]},
		TYPE_PACKED_VECTOR2_ARRAY,
	)
	_check(packed.ok and packed.result.size() == 2, "packed vector conversion failed")
	_check(not codec.convert(INF, TYPE_FLOAT).ok, "non-finite number was accepted")
	_check(not codec.convert(
		{"$type": "packed_byte_array", "values": [256]}, TYPE_PACKED_BYTE_ARRAY,
	).ok, "out-of-range packed byte was accepted")
	_check(not codec.convert("x".repeat(Limits.MAX_VALUE_STRING_CHARS + 1), TYPE_STRING).ok, "oversized string was accepted")
	var script_reference := codec.convert(
		{"$type": "resource", "path": "res://addons/godot_mcp/property_value_codec.gd"},
		TYPE_OBJECT, {"hint": PROPERTY_HINT_RESOURCE_TYPE, "hint_string": "Script"},
	)
	_check(script_reference.ok and script_reference.result is Script, "tagged resource reference failed")
	_check(not codec.convert(
		{"$type": "resource", "path": "res://addons/godot_mcp/property_value_codec.gd"},
		TYPE_OBJECT, {"hint": PROPERTY_HINT_RESOURCE_TYPE, "hint_string": "Texture2D"},
	).ok, "resource class mismatch was accepted")

	var enum_info := {"hint": PROPERTY_HINT_ENUM, "hint_string": "Idle:0,Run:2"}
	_check(codec.convert({"$type": "enum", "value": "Run"}, TYPE_INT, enum_info).result == 2, "enum name conversion failed")
	var flag_info := {"hint": PROPERTY_HINT_FLAGS, "hint_string": "Visible:1,Active:4"}
	_check(codec.convert({"$type": "flags", "value": ["Visible", "Active"]}, TYPE_INT, flag_info).result == 5, "flag conversion failed")

	var encoded_node = codec.encode(child, {}, root)
	_check(encoded_node == {"$type": "node", "path": "Child"}, "node encoding was not tagged")
	_check(codec.supported_forms().has("transform3d"), "capability forms omit Transform3D")
	_check(Limits.MAX_TRANSACTION_OPERATIONS == 64, "transaction operation limit drifted")

	var transaction_source := FileAccess.get_file_as_string("res://addons/godot_mcp/scene_transaction.gd")
	for operation in [
		"remove_node", "rename_node", "reparent_node", "attach_script",
		"detach_script", "connect_signal", "disconnect_signal", "add_to_group",
		"remove_from_group",
	]:
		_check(operation in transaction_source, "transaction operation missing: %s" % operation)
	_check("commit_action" in transaction_source and "postcondition" in transaction_source, "atomic commit safeguards are missing")
	root.free()

	var transaction_root := Node2D.new()
	transaction_root.name = "TransactionRoot"
	var fake_undo := FakeUndoRedo.new()
	var transaction = Transaction.new(
		FakeEditor.new(transaction_root), fake_undo, FakeProjectPaths.new(), codec,
	)
	var committed: Dictionary = transaction.transact({"operations": [
		{
			"op": "add_node", "parent": {"path": "."}, "type": "Node2D",
			"name": "Draft", "handle": "draft",
		},
		{"op": "rename_node", "target": {"handle": "draft"}, "name": "Final"},
		{"op": "set_property", "target": {"handle": "draft"}, "property": "position", "value": [8, 13]},
		{"op": "add_to_group", "target": {"handle": "draft"}, "group": "atomic"},
	]})
	_check(committed.ok, "transaction fixture did not commit")
	_check(committed.result.undo_version_after == committed.result.undo_version_before + 1, "transaction was not one undo version")
	_check(transaction_root.has_node("Final"), "transaction result path is missing")
	var final_node := transaction_root.get_node("Final") as Node2D
	_check(final_node.position == Vector2(8, 13) and final_node.is_in_group("atomic"), "transaction operations did not apply")
	fake_undo.undo()
	_check(not transaction_root.has_node("Final"), "single undo did not revert the complete transaction")
	fake_undo.redo()
	_check(transaction_root.has_node("Final"), "redo did not restore the transaction")
	final_node = transaction_root.get_node("Final") as Node2D
	_check(final_node.position == Vector2(8, 13) and final_node.is_in_group("atomic"), "redo did not restore transaction state")
	transaction_root.free()
	fake_undo.actions.clear()
	fake_undo.current_do.clear()
	fake_undo.current_undo.clear()
	transaction = null
	fake_undo = null

	var rollback_root := Node2D.new()
	rollback_root.name = "RollbackRoot"
	var rollback_undo := FakeUndoRedo.new()
	rollback_undo.skip_properties = true
	var rollback_transaction = Transaction.new(
		FakeEditor.new(rollback_root), rollback_undo, FakeProjectPaths.new(), codec,
	)
	var rolled_back: Dictionary = rollback_transaction.transact({"operations": [
		{
			"op": "add_node", "parent": {"path": "."}, "type": "Node2D",
			"name": "Unexpected", "handle": "unexpected",
		},
		{"op": "set_property", "target": {"handle": "unexpected"}, "property": "position", "value": [1, 2]},
	]})
	_check(not rolled_back.ok and rolled_back.error.code == "transaction_failed", "unexpected postcondition did not fail")
	_check(not rollback_root.has_node("Unexpected") and rollback_undo.version == -1, "postcondition failure did not undo immediately")
	var rollback_node := rollback_undo.actions[0].do[0].arguments[0] as Node
	rollback_root.free()
	rollback_undo.actions.clear()
	rollback_undo.current_do.clear()
	rollback_undo.current_undo.clear()
	rollback_node.free()
	rollback_transaction = null
	rollback_undo = null
	print("phase11_scene_transaction_test: PASS")
	quit()


func _check(condition: bool, message: String) -> void:
	if not condition:
		push_error(message)
		quit(1)
