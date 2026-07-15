extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")
const Limits := preload("command_limits.gd")

var _editor_interface: EditorInterface
var _project_paths: RefCounted
var _scene_nodes: RefCounted
var _property_values: RefCounted
var _transactions: RefCounted


func _init(
	editor_interface: EditorInterface,
	project_paths: RefCounted,
	scene_nodes: RefCounted,
	property_values: RefCounted,
	transactions: RefCounted,
) -> void:
	_editor_interface = editor_interface
	_project_paths = project_paths
	_scene_nodes = scene_nodes
	_property_values = property_values
	_transactions = transactions


func handlers() -> Dictionary:
	return {
		"create_scene": Callable(self, "_create_scene"),
		"add_node": Callable(self, "_add_node"),
		"instantiate_scene": Callable(self, "_instantiate_scene"),
		"set_property": Callable(self, "_set_property"),
		"select": Callable(self, "_select_node"),
	}


func _add_node(arguments: Dictionary) -> Dictionary:
	return _focused_transaction({
		"op": "add_node",
		"parent": {"path": arguments.get("parent")},
		"type": arguments.get("type"),
		"name": arguments.get("name"),
	}, "MCP: add %s" % str(arguments.get("name", "node")))


func _instantiate_scene(arguments: Dictionary) -> Dictionary:
	return _focused_transaction({
		"op": "instantiate_scene",
		"parent": {"path": arguments.get("parent")},
		"scene": arguments.get("scene"),
		"name": arguments.get("name"),
	}, "MCP: instantiate %s" % str(arguments.get("name", "scene")))


func _set_property(arguments: Dictionary) -> Dictionary:
	return _focused_transaction({
		"op": "set_property",
		"target": {"path": arguments.get("path")},
		"property": arguments.get("property"),
		"value": arguments.get("value") if arguments.has("value") else null,
	}, "MCP: set %s" % str(arguments.get("property", "property")), arguments.has("value"))


func _focused_transaction(operation: Dictionary, label: String, has_value := true) -> Dictionary:
	if not has_value:
		return ErrorEnvelope.failure("Missing value")
	var response: Dictionary = _transactions.transact({"operations": [operation], "label": label})
	if not response.ok:
		return response
	var result: Dictionary = response.result.results[0]
	# Preserve the compact focused-tool response while retaining the shared engine.
	if operation.op == "add_node" or operation.op == "instantiate_scene":
		return ErrorEnvelope.success({
			"path": result.path, "type": result.type, "name": result.name,
		})
	if operation.op == "set_property":
		return ErrorEnvelope.success({
			"path": result.path, "property": result.property, "value": result.value,
		})
	return ErrorEnvelope.success(result)


func _create_scene(arguments: Dictionary) -> Dictionary:
	var checked: Dictionary = _project_paths.check(arguments.get("path"), true, PackedStringArray(["tscn"]))
	if not checked.ok: return checked
	var path := checked.result as String
	if FileAccess.file_exists(path): return _failure("Scene already exists")
	if not DirAccess.dir_exists_absolute(ProjectSettings.globalize_path(path.get_base_dir())):
		return _failure("Scene folder does not exist")
	var root_result := _new_builtin_node(arguments.get("root_type"), arguments.get("root_name"), true)
	if not root_result.ok: return root_result
	var root := root_result.result as Node
	var context := {"count": 1, "specs": [{"node": root, "spec": arguments}]}
	var children = arguments.get("children", [])
	if not children is Array:
		root.free()
		return _failure("Scene children must be an array")
	var built := _build_children(root, children, root, context, 1)
	if not built.ok:
		root.free()
		return built
	for item in context.specs:
		var script_result := _apply_script(item.node, item.spec.get("script"))
		if not script_result.ok:
			root.free()
			return script_result
	for item in context.specs:
		var configured := _configure_created_node(item.node, item.spec, root)
		if not configured.ok:
			root.free()
			return configured
	var packed := PackedScene.new()
	var pack_error := packed.pack(root)
	if pack_error != OK:
		root.free()
		return _failure("Could not pack scene")
	var save_error := ResourceSaver.save(packed, path)
	root.free()
	if save_error != OK: return _failure("Could not save scene", ErrorEnvelope.SAVE_FAILED)
	_editor_interface.get_resource_filesystem().update_file(path)
	return ErrorEnvelope.success({
		"path": path,
		"root_type": arguments.root_type,
		"root_name": arguments.root_name,
		"node_count": context.count,
		"operation_count": _creation_operation_count(context.specs),
	})


func _build_children(parent: Node, children: Array, root: Node, context: Dictionary, depth: int) -> Dictionary:
	if depth > Limits.MAX_TRANSACTION_TREE_DEPTH:
		return _failure("Initial child tree exceeds the depth limit")
	if children.size() > Limits.MAX_TRANSACTION_CREATED_NODES:
		return _failure("Initial child list exceeds the node limit")
	for spec in children:
		if not spec is Dictionary or spec.size() > 8:
			return _failure("Each initial child must be a bounded object")
		context.count += 1
		if context.count > Limits.MAX_TRANSACTION_CREATED_NODES + 1:
			return _failure("Initial child tree exceeds the node limit")
		var created := _new_builtin_node(spec.get("type"), spec.get("name"), false)
		if not created.ok: return created
		var node := created.result as Node
		if parent.has_node(NodePath(str(node.name))):
			node.free()
			return _failure("Initial child names must be unique below each parent")
		parent.add_child(node)
		node.owner = root
		context.specs.append({"node": node, "spec": spec})
		var nested = spec.get("children", [])
		if not nested is Array: return _failure("Initial child children must be an array")
		var nested_result := _build_children(node, nested, root, context, depth + 1)
		if not nested_result.ok: return nested_result
	return ErrorEnvelope.success(true)


func _new_builtin_node(type_value: Variant, name_value: Variant, root: bool) -> Dictionary:
	var label := "Root type" if root else "Node type"
	if not type_value is String or type_value.is_empty() or type_value.length() > 128:
		return _failure("%s must be a non-empty string up to 128 characters" % label)
	if not ClassDB.class_exists(type_value) or not ClassDB.can_instantiate(type_value) or not ClassDB.is_parent_class(type_value, "Node"):
		return _failure("%s must be an instantiable built-in Node class" % label)
	var checked_name: Dictionary = _scene_nodes.checked_name(name_value)
	if not checked_name.ok: return checked_name
	var node := ClassDB.instantiate(type_value) as Node
	if node == null: return _failure("Could not instantiate node")
	node.name = checked_name.result
	return ErrorEnvelope.success(node)


func _apply_script(node: Node, value: Variant) -> Dictionary:
	if value == null: return ErrorEnvelope.success(true)
	var checked: Dictionary = _project_paths.check(value, false, PackedStringArray(["gd", "cs"]))
	if not checked.ok: return checked
	var script := ResourceLoader.load(checked.result) as Script
	if script == null: return _failure("Script resource not found")
	var base_type := script.get_instance_base_type()
	if not base_type.is_empty() and not node.is_class(base_type):
		return _failure("Script base type is not compatible with the node")
	node.set_script(script)
	return ErrorEnvelope.success(true)


func _configure_created_node(node: Node, spec: Dictionary, root: Node) -> Dictionary:
	var groups = spec.get("groups", [])
	if not groups is Array or groups.size() > 32: return _failure("Groups must be an array with at most 32 entries")
	for group in groups:
		if not group is String or group.is_empty() or group.length() > 128:
			return _failure("Group names must be non-empty strings up to 128 characters")
		if not node.is_in_group(group): node.add_to_group(group, true)
	var properties = spec.get("properties", {})
	if not properties is Dictionary or properties.size() > 32:
		return _failure("Properties must be an object with at most 32 entries")
	for property_name in properties:
		if not property_name is String or property_name.is_empty() or property_name.length() > 128:
			return _failure("Property name is invalid")
		var info := _editable_property(node, property_name)
		if info.is_empty(): return _failure("Editable property not found: %s" % property_name)
		var converted: Dictionary = _property_values.convert(properties[property_name], int(info.type), info, root)
		if not converted.ok: return converted
		node.set(property_name, converted.result)
	return ErrorEnvelope.success(true)


func _editable_property(node: Node, property_name: String) -> Dictionary:
	for info in node.get_property_list():
		if str(info.name) == property_name and (int(info.usage) & PROPERTY_USAGE_EDITOR) != 0:
			return info
	return {}


func _creation_operation_count(specs: Array) -> int:
	var count := maxi(0, specs.size() - 1)
	for item in specs:
		if item.spec.get("script") != null: count += 1
		count += item.spec.get("groups", []).size()
		count += item.spec.get("properties", {}).size()
	return count


func _select_node(arguments: Dictionary) -> Dictionary:
	var found: Dictionary = _scene_nodes.find(arguments.get("path"))
	if not found.ok: return found
	var selection := _editor_interface.get_selection()
	selection.clear()
	selection.add_node(found.result)
	return ErrorEnvelope.success("Selected %s" % arguments.path)


func _failure(message: String, code := ErrorEnvelope.INVALID_ARGUMENT) -> Dictionary:
	return ErrorEnvelope.failure(message, code)
