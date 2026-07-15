extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")
const Limits := preload("command_limits.gd")
const MAX_TREE_NODES := Limits.MAX_TREE_NODES
const MAX_TREE_DEPTH := Limits.MAX_TREE_DEPTH
const MAX_TREE_SCAN := Limits.MAX_TREE_SCAN
const MAX_PROPERTIES := Limits.MAX_PROPERTIES
const MAX_PROPERTY_SCAN := Limits.MAX_PROPERTY_SCAN

var _editor_interface: EditorInterface
var _undo_redo: EditorUndoRedoManager
var _scene_nodes: RefCounted
var _property_values: RefCounted
var _cursors: RefCounted
var _runtime_inspector: Variant


func _init(
	editor_interface: EditorInterface,
	undo_redo: EditorUndoRedoManager,
	scene_nodes: RefCounted,
	property_values: RefCounted,
	cursors: RefCounted,
	runtime_inspector: Variant = null,
) -> void:
	_editor_interface = editor_interface
	_undo_redo = undo_redo
	_scene_nodes = scene_nodes
	_property_values = property_values
	_cursors = cursors
	_runtime_inspector = runtime_inspector


func handlers() -> Dictionary:
	return {
		"tree": Callable(self, "_scene_tree"),
		"inspect": Callable(self, "_inspect_node"),
	}


func _scene_tree(arguments: Dictionary) -> Dictionary:
	var scope_result := _tree_scope(arguments)
	if not scope_result.ok:
		return scope_result
	if scope_result.result == "runtime":
		return _runtime_inspector.scene_tree(arguments)
	var root := _editor_interface.get_edited_scene_root()
	if root == null:
		return _failure("No scene is open")
	var root_path = arguments.get("root", ".")
	var found: Dictionary = _scene_nodes.find(root_path)
	if not found.ok:
		return found
	var target := found.result as Node
	var normalized_root_path := "." if target == root else str(root.get_path_to(target))
	var depth_result := _bounded_integer(
		arguments.get("max_depth", 3), "Maximum depth", 0, MAX_TREE_DEPTH,
	)
	if not depth_result.ok:
		return depth_result
	var class_filter = arguments.get("class", "")
	if not class_filter is String or class_filter.length() > 128:
		return _failure("Class filter must be a string up to 128 characters")
	if arguments.has("class") and class_filter.is_empty():
		return _failure("Class filter cannot be empty")
	var limit_result := _bounded_integer(
		arguments.get("limit", 50), "Limit", 1, MAX_TREE_NODES,
	)
	if not limit_result.ok:
		return limit_result
	var max_depth: int = depth_result.result
	var limit: int = limit_result.result
	var query := [normalized_root_path, max_depth, class_filter, limit]
	var snapshot := _tree_snapshot(root)
	var offset_result := _cursor_offset(arguments, "scene_tree", query, snapshot)
	if not offset_result.ok:
		return offset_result
	var offset: int = offset_result.result
	var nodes: Array[Dictionary] = []
	var state := {
		"visited": 0, "matched": 0, "has_more": false, "scan_exhausted": false,
	}
	_collect_nodes(
		root, target, nodes, 0, max_depth, class_filter, offset, limit, state,
	)
	var continuation_available: bool = state.has_more
	var cursor: Variant = null
	if continuation_available:
		cursor = _cursors.issue("scene_tree", query, snapshot, offset + nodes.size())
	return _success({
		"scope": "edited",
		"root": normalized_root_path,
		"nodes": nodes,
		"truncated": continuation_available or state.scan_exhausted,
		"snapshot_id": snapshot,
		"continuation_available": continuation_available,
		"cursor": cursor,
	})


func _collect_nodes(
	root: Node,
	node: Node,
	output: Array[Dictionary],
	depth: int,
	max_depth: int,
	class_filter: String,
	offset: int,
	limit: int,
	state: Dictionary,
) -> bool:
	if state.visited >= MAX_TREE_SCAN:
		state.scan_exhausted = true
		return true
	state.visited += 1
	if class_filter.is_empty() or node.get_class() == class_filter:
		if state.matched < offset:
			state.matched += 1
		elif output.size() < limit:
			output.append({
				"path": "." if node == root else str(root.get_path_to(node)),
				"type": node.get_class(),
				"depth": depth,
			})
			state.matched += 1
		else:
			state.has_more = true
			return true
	if depth >= max_depth:
		return false
	for child in node.get_children():
		if _collect_nodes(
			root, child, output, depth + 1, max_depth, class_filter,
			offset, limit, state,
		):
			return true
	return false


func _inspect_node(arguments: Dictionary) -> Dictionary:
	var scope_result := _tree_scope(arguments)
	if not scope_result.ok:
		return scope_result
	if scope_result.result == "runtime":
		return _runtime_inspector.inspect_node(arguments)
	if arguments.has("runtime_id"):
		return _failure("runtime_id is valid only for runtime inspection")
	var found: Dictionary = _scene_nodes.find(arguments.get("path"))
	if not found.ok:
		return found
	var node := found.result as Node
	var root := _editor_interface.get_edited_scene_root()
	var normalized_path := "." if node == root else str(root.get_path_to(node))
	var property_filter = arguments.get("property", "")
	if not property_filter is String or property_filter.length() > 128:
		return _failure("Property filter must be a string up to 128 characters")
	if arguments.has("property") and property_filter.is_empty():
		return _failure("Property filter cannot be empty")
	var category_filter = arguments.get("category", "")
	if not category_filter is String or category_filter.length() > 128:
		return _failure("Category filter must be a string up to 128 characters")
	if arguments.has("category") and category_filter.is_empty():
		return _failure("Category filter cannot be empty")
	var limit_result := _bounded_integer(
		arguments.get("limit", 24), "Limit", 1, MAX_PROPERTIES,
	)
	if not limit_result.ok:
		return limit_result
	var descriptors: Array[Dictionary] = []
	var fingerprint_parts: Array = []
	var category := ""
	var scanned := 0
	var scan_exhausted := false
	for info in node.get_property_list():
		if scanned >= MAX_PROPERTY_SCAN:
			scan_exhausted = true
			break
		scanned += 1
		var usage := int(info.get("usage", 0))
		var property_name := str(info.get("name", ""))
		fingerprint_parts.append([
			property_name, int(info.get("type", TYPE_NIL)), usage,
			int(info.get("hint", PROPERTY_HINT_NONE)), str(info.get("hint_string", "")),
		])
		if (usage & PROPERTY_USAGE_CATEGORY) != 0:
			category = property_name
			continue
		if (usage & PROPERTY_USAGE_EDITOR) == 0:
			continue
		descriptors.append({
			"name": property_name,
			"type": type_string(int(info.get("type", TYPE_NIL))),
			"category": category,
		})
	var query := [normalized_path, property_filter, category_filter, int(limit_result.result)]
	var snapshot: String = _cursors.snapshot_id("node_properties", [
		"" if root == null else root.scene_file_path,
		node.get_instance_id(),
		normalized_path,
		fingerprint_parts,
	])
	var offset_result := _cursor_offset(arguments, "node_properties", query, snapshot)
	if not offset_result.ok:
		return offset_result
	var offset: int = offset_result.result
	var properties: Array[Dictionary] = []
	var matched := 0
	var has_more := false
	for descriptor in descriptors:
		if not property_filter.is_empty() and descriptor.name != property_filter:
			continue
		if not category_filter.is_empty() and descriptor.category != category_filter:
			continue
		if matched < offset:
			matched += 1
			continue
		if properties.size() >= int(limit_result.result):
			has_more = true
			break
		var result_property: Dictionary = descriptor.duplicate()
		result_property["value"] = _property_values.encode(
			node.get(descriptor.name), {}, root,
		)
		properties.append(result_property)
		matched += 1
	var continuation_available := has_more
	var cursor: Variant = null
	if continuation_available:
		cursor = _cursors.issue(
			"node_properties", query, snapshot, offset + properties.size(),
		)
	return _success({
		"scope": "edited",
		"path": normalized_path,
		"type": node.get_class(),
		"properties": properties,
		"truncated": continuation_available or scan_exhausted,
		"snapshot_id": snapshot,
		"continuation_available": continuation_available,
		"cursor": cursor,
	})


func _tree_scope(arguments: Dictionary) -> Dictionary:
	var scope = arguments.get("tree_scope", "edited")
	if not scope is String or scope not in ["edited", "runtime"]:
		return _failure("tree_scope must be edited or runtime")
	if scope == "runtime" and _runtime_inspector == null:
		return ErrorEnvelope.failure(
			"Runtime inspection is unavailable",
			ErrorEnvelope.RUNTIME_PROBE_UNAVAILABLE, {}, true,
		)
	return _success(scope)


func _tree_snapshot(root: Node) -> String:
	var history_id: int = _undo_redo.get_object_history_id(root)
	var history = _undo_redo.get_history_undo_redo(history_id)
	var version := 0 if history == null else int(history.get_version())
	var hashing := HashingContext.new()
	hashing.start(HashingContext.HASH_SHA256)
	var state := {"visited": 0}
	_hash_tree(root, root, hashing, state)
	var structure_fingerprint := hashing.finish().hex_encode()
	return _cursors.snapshot_id(
		"edited_scene_tree", [
			root.scene_file_path, root.get_instance_id(), version, structure_fingerprint,
		],
	)


func _hash_tree(
	root: Node, node: Node, hashing: HashingContext, state: Dictionary,
) -> void:
	if state.visited >= MAX_TREE_SCAN:
		return
	state.visited += 1
	var path := "." if node == root else str(root.get_path_to(node))
	hashing.update((JSON.stringify([path, node.get_class()]) + "\n").to_utf8_buffer())
	for child in node.get_children():
		_hash_tree(root, child, hashing, state)


func _cursor_offset(
	arguments: Dictionary, kind: String, query: Variant, snapshot: String,
) -> Dictionary:
	if not arguments.has("cursor"):
		return _success(0)
	return _cursors.resume(arguments.cursor, kind, query, snapshot)


func _bounded_integer(
	value: Variant, label: String, minimum: int, maximum: int,
) -> Dictionary:
	if (not value is int and not value is float) or float(value) != floorf(float(value)):
		return _failure("%s must be an integer" % label)
	var integer := int(value)
	if integer < minimum or integer > maximum:
		return _failure("%s must be between %d and %d" % [label, minimum, maximum])
	return _success(integer)


func _success(result: Variant) -> Dictionary:
	return ErrorEnvelope.success(result)


func _failure(message: String) -> Dictionary:
	return ErrorEnvelope.failure(message)
