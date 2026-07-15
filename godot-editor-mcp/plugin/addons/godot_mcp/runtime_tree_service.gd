extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")
const Limits := preload("command_limits.gd")
const PropertyValueCodec := preload("property_value_codec.gd")

var _context
var _property_values := PropertyValueCodec.new()
var _tree_fingerprint := ""
var _tree_generation := 0


func _init(context) -> void:
	_context = context


func scene_tree(arguments: Dictionary) -> Dictionary:
	var resolved: Dictionary = _context.resolve_node(arguments.get("root", "."))
	if not resolved.ok:
		return resolved
	var root: Node = _context.ignored_node.get_tree().current_scene
	var target := resolved.result as Node
	var depth_result: Dictionary = _context.bounded_integer(
		arguments.get("max_depth", 3), "Maximum depth", 0, Limits.MAX_TREE_DEPTH,
	)
	if not depth_result.ok:
		return depth_result
	var class_filter = arguments.get("class", "")
	if not class_filter is String or class_filter.length() > 128:
		return _context.failure("Class filter must be a string up to 128 characters")
	if arguments.has("class") and class_filter.is_empty():
		return _context.failure("Class filter cannot be empty")
	var limit_result: Dictionary = _context.bounded_integer(
		arguments.get("limit", 50), "Limit", 1, Limits.MAX_TREE_NODES,
	)
	if not limit_result.ok:
		return limit_result
	var offset_result: Dictionary = _context.bounded_integer(
		arguments.get("_offset", 0), "Offset", 0, Limits.MAX_TREE_SCAN,
	)
	if not offset_result.ok:
		return offset_result
	var expected_result: Dictionary = _context.expected_snapshot(arguments)
	if not expected_result.ok:
		return expected_result
	var snapshot := _refresh_tree_snapshot(root)
	if not expected_result.result.is_empty() and expected_result.result != snapshot:
		return ErrorEnvelope.failure(
			"Runtime tree cursor is stale", ErrorEnvelope.STALE_CURSOR,
			{"current_snapshot": snapshot}, false,
		)
	var nodes: Array[Dictionary] = []
	var state := {
		"visited": 0, "matched": 0, "has_more": false, "scan_exhausted": false,
	}
	_collect_nodes(
		root, target, nodes, 0, int(depth_result.result), class_filter,
		int(offset_result.result), int(limit_result.result), state,
	)
	return _context.success({
		"scope": "runtime",
		"run_id": _context.run_id,
		"debugger_session_id": _context.debugger_session_id,
		"tree_generation": _tree_generation,
		"root": _context.node_path(root, target),
		"nodes": nodes,
		"truncated": bool(state.has_more) or bool(state.scan_exhausted),
		"snapshot_id": snapshot,
		"continuation_available": bool(state.has_more),
		"cursor": null,
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
	if state.visited >= Limits.MAX_TREE_SCAN:
		state.scan_exhausted = true
		return true
	state.visited += 1
	if class_filter.is_empty() or node.get_class() == class_filter:
		if state.matched < offset:
			state.matched += 1
		elif output.size() < limit:
			output.append(_node_metadata(root, node, depth))
			state.matched += 1
		else:
			state.has_more = true
			return true
	if depth >= max_depth:
		return false
	for child in node.get_children():
		if child == _context.ignored_node:
			continue
		if _collect_nodes(
			root, child, output, depth + 1, max_depth, class_filter,
			offset, limit, state,
		):
			return true
	return false


func inspect_node(arguments: Dictionary) -> Dictionary:
	var resolved: Dictionary = _context.resolve_node(arguments.get("path"))
	if not resolved.ok:
		return resolved
	var node := resolved.result as Node
	var root: Node = _context.ignored_node.get_tree().current_scene
	var runtime_id: String = _context.runtime_id(node)
	if arguments.has("runtime_id"):
		var requested_id = arguments.runtime_id
		if (
			not requested_id is String
			or requested_id.length() != 64
			or not requested_id.is_valid_hex_number()
		):
			return _context.failure("runtime_id must be a 64-character hexadecimal string")
		if requested_id != runtime_id:
			return ErrorEnvelope.failure(
				"Runtime node identity is stale", ErrorEnvelope.STALE_RUNTIME_ID,
			)
	var property_filter = arguments.get("property", "")
	if not property_filter is String or property_filter.length() > 128:
		return _context.failure("Property filter must be a string up to 128 characters")
	if arguments.has("property") and property_filter.is_empty():
		return _context.failure("Property filter cannot be empty")
	var category_filter = arguments.get("category", "")
	if not category_filter is String or category_filter.length() > 128:
		return _context.failure("Category filter must be a string up to 128 characters")
	if arguments.has("category") and category_filter.is_empty():
		return _context.failure("Category filter cannot be empty")
	var limit_result: Dictionary = _context.bounded_integer(
		arguments.get("limit", 24), "Limit", 1, Limits.MAX_PROPERTIES,
	)
	if not limit_result.ok:
		return limit_result
	var offset_result: Dictionary = _context.bounded_integer(
		arguments.get("_offset", 0), "Offset", 0, Limits.MAX_PROPERTY_SCAN,
	)
	if not offset_result.ok:
		return offset_result
	var expected_result: Dictionary = _context.expected_snapshot(arguments)
	if not expected_result.ok:
		return expected_result
	var descriptors: Array[Dictionary] = []
	var fingerprint_parts: Array = []
	var category := ""
	var scanned := 0
	var scan_exhausted := false
	for info in node.get_property_list():
		if scanned >= Limits.MAX_PROPERTY_SCAN:
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
	var tree_snapshot := _refresh_tree_snapshot(root)
	var snapshot := JSON.stringify([
		tree_snapshot, runtime_id, fingerprint_parts,
	]).sha256_text()
	if not expected_result.result.is_empty() and expected_result.result != snapshot:
		return ErrorEnvelope.failure(
			"Runtime property cursor is stale", ErrorEnvelope.STALE_CURSOR,
			{"current_snapshot": snapshot}, false,
		)
	var properties: Array[Dictionary] = []
	var matched := 0
	var has_more := false
	for descriptor in descriptors:
		if not property_filter.is_empty() and descriptor.name != property_filter:
			continue
		if not category_filter.is_empty() and descriptor.category != category_filter:
			continue
		if matched < int(offset_result.result):
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
	var result := _node_metadata(root, node, 0)
	result["scope"] = "runtime"
	result["run_id"] = _context.run_id
	result["debugger_session_id"] = _context.debugger_session_id
	result["tree_generation"] = _tree_generation
	result["properties"] = properties
	result["truncated"] = has_more or scan_exhausted
	result["snapshot_id"] = snapshot
	result["continuation_available"] = has_more
	result["cursor"] = null
	return _context.success(result)


func _node_metadata(root: Node, node: Node, depth: int) -> Dictionary:
	var path: String = _context.node_path(root, node)
	var parent_path: Variant = null
	if node != root:
		parent_path = _context.node_path(root, node.get_parent())
	var script_path: Variant = null
	var script = node.get_script()
	if script is Script and not script.resource_path.is_empty():
		script_path = script.resource_path.left(512)
	var source_scene: Variant = null
	if not node.scene_file_path.is_empty():
		source_scene = node.scene_file_path.left(512)
	var groups: Array[String] = []
	for group in node.get_groups():
		var group_name := str(group)
		if group_name.begins_with("_"):
			continue
		groups.append(group_name.left(64))
		if groups.size() >= Limits.MAX_RUNTIME_GROUPS:
			break
	groups.sort()
	var visible: Variant = null
	if node is CanvasItem:
		visible = node.is_visible_in_tree()
	elif node is Node3D:
		visible = node.is_visible_in_tree()
	return {
		"runtime_id": _context.runtime_id(node),
		"path": path,
		"name": str(node.name).left(128),
		"type": node.get_class().left(128),
		"parent": parent_path,
		"script": script_path,
		"source_scene": source_scene,
		"groups": groups,
		"process_mode": int(node.process_mode),
		"visible": visible,
		"depth": depth,
	}


func _refresh_tree_snapshot(root: Node) -> String:
	var hashing := HashingContext.new()
	hashing.start(HashingContext.HASH_SHA256)
	var state := {"visited": 0}
	_hash_tree(root, root, hashing, state)
	var fingerprint := hashing.finish().hex_encode()
	if fingerprint != _tree_fingerprint:
		_tree_fingerprint = fingerprint
		_tree_generation += 1
	return JSON.stringify([
		_context.project_hash, _context.run_id, _context.debugger_session_id,
		_tree_generation, fingerprint,
	]).sha256_text()


func _hash_tree(
	root: Node, node: Node, hashing: HashingContext, state: Dictionary,
) -> void:
	if state.visited >= Limits.MAX_TREE_SCAN:
		return
	state.visited += 1
	hashing.update((JSON.stringify([
		_context.node_path(root, node), node.get_class(), _context.runtime_id(node),
	]) + "\n").to_utf8_buffer())
	for child in node.get_children():
		if child != _context.ignored_node:
			_hash_tree(root, child, hashing, state)
