extends "command_base.gd"

const Limits := preload("command_limits.gd")
const MAX_TREE_NODES := Limits.MAX_TREE_NODES
const MAX_PROPERTIES := Limits.MAX_PROPERTIES


func execute(command: String, arguments: Dictionary) -> Dictionary:
	match command:
		"tree":
			return _scene_tree()
		"inspect":
			return _inspect_node(arguments)
		"add_node":
			return _add_node(arguments)
		"instantiate_scene":
			return _instantiate_scene(arguments)
		"set_property":
			return _set_property(arguments)
		"select":
			return _select_node(arguments)
		_:
			return _failure("Unknown scene command")


func _scene_tree() -> Dictionary:
	var root := get_editor_interface().get_edited_scene_root()
	if root == null:
		return _failure("No scene is open")
	var nodes: Array[Dictionary] = []
	_collect_nodes(root, root, nodes, 0)
	return _success({"nodes": nodes, "truncated": nodes.size() >= MAX_TREE_NODES})


func _collect_nodes(root: Node, node: Node, output: Array[Dictionary], depth: int) -> void:
	if output.size() >= MAX_TREE_NODES:
		return
	output.append({
		"path": "." if node == root else str(root.get_path_to(node)),
		"type": node.get_class(),
		"depth": depth,
	})
	for child in node.get_children():
		if output.size() >= MAX_TREE_NODES:
			return
		_collect_nodes(root, child, output, depth + 1)


func _inspect_node(arguments: Dictionary) -> Dictionary:
	var found := _find_node(arguments.get("path"))
	if not found.ok:
		return found
	var node := found.result as Node
	var properties: Array[Dictionary] = []
	for info in node.get_property_list():
		if properties.size() >= MAX_PROPERTIES:
			break
		var usage := int(info.get("usage", 0))
		if (usage & PROPERTY_USAGE_EDITOR) == 0:
			continue
		var property_name := str(info.name)
		properties.append({
			"name": property_name,
			"type": type_string(int(info.type)),
			"value": _encode_value(node.get(property_name)),
		})
	return _success({
		"path": arguments.path,
		"type": node.get_class(),
		"properties": properties,
		"truncated": properties.size() >= MAX_PROPERTIES,
	})


func _add_node(arguments: Dictionary) -> Dictionary:
	var found := _find_node(arguments.get("parent"))
	if not found.ok:
		return found
	var node_type = arguments.get("type")
	if not node_type is String or node_type.is_empty() or node_type.length() > 128:
		return _failure("Node type must be a non-empty string up to 128 characters")
	if not ClassDB.class_exists(node_type) or not ClassDB.can_instantiate(node_type) or not ClassDB.is_parent_class(node_type, "Node"):
		return _failure("Type must be an instantiable built-in Node class")
	var valid_name := _checked_node_name(arguments.get("name"))
	if not valid_name.ok:
		return valid_name
	var parent := found.result as Node
	if _has_child_named(parent, valid_name.result):
		return _failure("Parent already has a child with that name")
	var object = ClassDB.instantiate(node_type)
	if not object is Node:
		if object != null:
			object.free()
		return _failure("Could not instantiate node")
	var node := object as Node
	node.name = valid_name.result
	return _commit_added_node(parent, node, "MCP: add %s" % valid_name.result)


func _instantiate_scene(arguments: Dictionary) -> Dictionary:
	var found := _find_node(arguments.get("parent"))
	if not found.ok:
		return found
	var checked := _project_path(arguments.get("scene"), false, PackedStringArray(["tscn", "scn"]))
	if not checked.ok:
		return checked
	var scene_path := checked.result as String
	var edited_root := get_editor_interface().get_edited_scene_root()
	if edited_root.scene_file_path == scene_path:
		return _failure("Cannot instantiate the edited scene inside itself")
	var valid_name := _checked_node_name(arguments.get("name"))
	if not valid_name.ok:
		return valid_name
	var parent := found.result as Node
	if _has_child_named(parent, valid_name.result):
		return _failure("Parent already has a child with that name")
	var resource := ResourceLoader.load(scene_path)
	if not resource is PackedScene:
		return _failure("PackedScene not found")
	var node := (resource as PackedScene).instantiate(PackedScene.GEN_EDIT_STATE_INSTANCE)
	if node == null:
		return _failure("Could not instantiate scene")
	node.name = valid_name.result
	return _commit_added_node(parent, node, "MCP: instantiate %s" % valid_name.result)


func _commit_added_node(parent: Node, node: Node, action: String) -> Dictionary:
	var root := get_editor_interface().get_edited_scene_root()
	var undo := get_undo_redo()
	undo.create_action(action)
	undo.add_do_method(parent, "add_child", node)
	undo.add_do_method(node, "set_owner", root)
	undo.add_do_reference(node)
	undo.add_undo_method(parent, "remove_child", node)
	undo.commit_action()
	return _success({
		"path": str(root.get_path_to(node)),
		"type": node.get_class(),
		"name": node.name,
	})


func _has_child_named(parent: Node, node_name: String) -> bool:
	for child in parent.get_children():
		if child.name == node_name:
			return true
	return false


func _set_property(arguments: Dictionary) -> Dictionary:
	var found := _find_node(arguments.get("path"))
	if not found.ok:
		return found
	var property_name = arguments.get("property")
	if not property_name is String or property_name.is_empty() or property_name.length() > 128:
		return _failure("Property must be a non-empty string up to 128 characters")
	if not arguments.has("value"):
		return _failure("Missing value")
	var node := found.result as Node
	var property_info: Dictionary = {}
	for info in node.get_property_list():
		if str(info.name) == property_name and (int(info.usage) & PROPERTY_USAGE_EDITOR) != 0:
			property_info = info
			break
	if property_info.is_empty():
		return _failure("Editable property not found")
	var converted := _convert_value(arguments.value, int(property_info.type))
	if not converted.ok:
		return converted
	var previous = node.get(property_name)
	var undo := get_undo_redo()
	undo.create_action("MCP: set %s" % property_name)
	undo.add_do_property(node, property_name, converted.result)
	undo.add_undo_property(node, property_name, previous)
	undo.commit_action()
	return _success({"path": arguments.path, "property": property_name, "value": _encode_value(node.get(property_name))})


func _select_node(arguments: Dictionary) -> Dictionary:
	var found := _find_node(arguments.get("path"))
	if not found.ok:
		return found
	var selection := get_editor_interface().get_selection()
	selection.clear()
	selection.add_node(found.result)
	return _success("Selected %s" % arguments.path)
