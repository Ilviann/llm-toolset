extends Node

const ErrorEnvelope := preload("error_envelope.gd")
const Limits := preload("command_limits.gd")
const ProjectIdentity := preload("project_identity.gd")
const PropertyValueCodec := preload("property_value_codec.gd")

const CAPTURE := "godot_mcp"
const PROBE_VERSION := "1"
const COMMANDS := ["inspect", "tree"]
const HANDSHAKE_RETRY_MSEC := 250

var _registered := false
var _run_id := 0
var _debugger_session_id := 0
var _project_hash := ""
var _instance_nonce := ""
var _tree_fingerprint := ""
var _tree_generation := 0
var _last_hello_msec := -HANDSHAKE_RETRY_MSEC
var _property_values := PropertyValueCodec.new()


func _enter_tree() -> void:
	set_process(false)
	set_physics_process(false)
	if not EngineDebugger.is_active() or EngineDebugger.has_capture(CAPTURE):
		return
	_project_hash = ProjectIdentity.current_hash()
	_instance_nonce = Crypto.new().generate_random_bytes(16).hex_encode()
	EngineDebugger.register_message_capture(CAPTURE, Callable(self, "_capture"))
	_registered = true
	set_process(true)
	_send_hello()


func _process(_delta: float) -> void:
	if _registered and _run_id < 1:
		_send_hello()


func _send_hello() -> void:
	var now := int(Time.get_ticks_msec())
	if now - _last_hello_msec < HANDSHAKE_RETRY_MSEC:
		return
	_last_hello_msec = now
	EngineDebugger.send_message(CAPTURE + ":hello", [{
		"project_hash": _project_hash,
		"probe_version": PROBE_VERSION,
		"commands": COMMANDS.duplicate(),
		"limits": {
			"tree_nodes": Limits.MAX_TREE_NODES,
			"tree_depth": Limits.MAX_TREE_DEPTH,
			"tree_scan": Limits.MAX_TREE_SCAN,
			"properties": Limits.MAX_PROPERTIES,
			"property_scan": Limits.MAX_PROPERTY_SCAN,
		},
		"instance_nonce": _instance_nonce,
	}])


func _exit_tree() -> void:
	if _registered and EngineDebugger.has_capture(CAPTURE):
		EngineDebugger.unregister_message_capture(CAPTURE)
	_registered = false
	_run_id = 0
	_debugger_session_id = 0


func _capture(message: String, data: Array) -> bool:
	if data.size() != 1 or not data[0] is Dictionary:
		return true
	var payload := data[0] as Dictionary
	match message:
		"accept":
			_accept(payload)
		"reject":
			_run_id = 0
			_debugger_session_id = 0
			set_process(false)
		"request":
			_respond(payload)
	return true


func _accept(payload: Dictionary) -> void:
	if (
		payload.get("project_hash") != _project_hash
		or payload.get("probe_version") != PROBE_VERSION
		or payload.get("instance_nonce") != _instance_nonce
		or not payload.get("run_id") is int
		or int(payload.run_id) < 1
		or not payload.get("debugger_session_id") is int
		or int(payload.debugger_session_id) < 0
	):
		return
	_run_id = int(payload.run_id)
	_debugger_session_id = int(payload.debugger_session_id)
	set_process(false)
	EngineDebugger.send_message(CAPTURE + ":handshake", [{
		"run_id": _run_id,
		"debugger_session_id": _debugger_session_id,
		"project_hash": _project_hash,
		"probe_version": PROBE_VERSION,
		"instance_nonce": _instance_nonce,
	}])


func _respond(payload: Dictionary) -> void:
	var request_id = payload.get("request_id")
	if not request_id is String or request_id.length() != 32:
		return
	var response: Dictionary
	if (
		_run_id < 1
		or payload.get("run_id") != _run_id
		or payload.get("debugger_session_id") != _debugger_session_id
		or payload.get("project_hash") != _project_hash
		or payload.get("probe_version") != PROBE_VERSION
	):
		response = ErrorEnvelope.failure(
			"Runtime request identity is stale", ErrorEnvelope.STALE_RUNTIME_ID,
		)
	else:
		var arguments = payload.get("arguments")
		if not arguments is Dictionary:
			response = _failure("Runtime inspection arguments are invalid")
		elif payload.get("command") == "tree":
			response = _scene_tree(arguments)
		elif payload.get("command") == "inspect":
			response = _inspect_node(arguments)
		else:
			response = _failure("Runtime inspection command is unsupported")
	var message := {
		"request_id": request_id,
		"run_id": _run_id,
		"debugger_session_id": _debugger_session_id,
		"project_hash": _project_hash,
		"probe_version": PROBE_VERSION,
		"response": response,
	}
	if JSON.stringify(message).to_utf8_buffer().size() > Limits.MAX_RESPONSE_BYTES:
		message.response = ErrorEnvelope.failure(
			"Runtime inspection response is too large",
			ErrorEnvelope.INVALID_ARGUMENT,
			{"limit": Limits.MAX_RESPONSE_BYTES}, false,
		)
	EngineDebugger.send_message(CAPTURE + ":response", [message])


func _scene_tree(arguments: Dictionary) -> Dictionary:
	var resolved := _resolve_node(arguments.get("root", "."))
	if not resolved.ok:
		return resolved
	var root := get_tree().current_scene
	var target := resolved.result as Node
	var depth_result := _bounded_integer(
		arguments.get("max_depth", 3), "Maximum depth", 0, Limits.MAX_TREE_DEPTH,
	)
	if not depth_result.ok:
		return depth_result
	var class_filter = arguments.get("class", "")
	if not class_filter is String or class_filter.length() > 128:
		return _failure("Class filter must be a string up to 128 characters")
	if arguments.has("class") and class_filter.is_empty():
		return _failure("Class filter cannot be empty")
	var limit_result := _bounded_integer(
		arguments.get("limit", 50), "Limit", 1, Limits.MAX_TREE_NODES,
	)
	if not limit_result.ok:
		return limit_result
	var offset_result := _bounded_integer(
		arguments.get("_offset", 0), "Offset", 0, Limits.MAX_TREE_SCAN,
	)
	if not offset_result.ok:
		return offset_result
	var expected_result := _expected_snapshot(arguments)
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
	var normalized_root := _node_path(root, target)
	return _success({
		"scope": "runtime",
		"run_id": _run_id,
		"debugger_session_id": _debugger_session_id,
		"tree_generation": _tree_generation,
		"root": normalized_root,
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
		if child == self:
			continue
		if _collect_nodes(
			root, child, output, depth + 1, max_depth, class_filter,
			offset, limit, state,
		):
			return true
	return false


func _inspect_node(arguments: Dictionary) -> Dictionary:
	var resolved := _resolve_node(arguments.get("path"))
	if not resolved.ok:
		return resolved
	var node := resolved.result as Node
	var root := get_tree().current_scene
	var runtime_id := _runtime_id(node)
	if arguments.has("runtime_id"):
		var requested_id = arguments.runtime_id
		if (
			not requested_id is String
			or requested_id.length() != 64
			or not requested_id.is_valid_hex_number()
		):
			return _failure("runtime_id must be a 64-character hexadecimal string")
		if requested_id != runtime_id:
			return ErrorEnvelope.failure(
				"Runtime node identity is stale", ErrorEnvelope.STALE_RUNTIME_ID,
			)
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
		arguments.get("limit", 24), "Limit", 1, Limits.MAX_PROPERTIES,
	)
	if not limit_result.ok:
		return limit_result
	var offset_result := _bounded_integer(
		arguments.get("_offset", 0), "Offset", 0, Limits.MAX_PROPERTY_SCAN,
	)
	if not offset_result.ok:
		return offset_result
	var expected_result := _expected_snapshot(arguments)
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
		result_property["value"] = _property_values.encode(node.get(descriptor.name))
		properties.append(result_property)
		matched += 1
	var result := _node_metadata(root, node, 0)
	result["scope"] = "runtime"
	result["run_id"] = _run_id
	result["debugger_session_id"] = _debugger_session_id
	result["tree_generation"] = _tree_generation
	result["properties"] = properties
	result["truncated"] = has_more or scan_exhausted
	result["snapshot_id"] = snapshot
	result["continuation_available"] = has_more
	result["cursor"] = null
	return _success(result)


func _resolve_node(path_value: Variant) -> Dictionary:
	var root := get_tree().current_scene
	if root == null:
		return ErrorEnvelope.failure(
			"No runtime scene is active", ErrorEnvelope.NO_ACTIVE_RUN,
		)
	if not path_value is String or path_value.is_empty() or path_value.length() > 512:
		return _failure("Runtime node path must be a string up to 512 characters")
	if (
		path_value.begins_with("/")
		or path_value == ".."
		or path_value.begins_with("../")
		or "/../" in path_value
	):
		return _failure("Runtime node path must remain inside the running scene")
	var node: Node = root if path_value == "." else root.get_node_or_null(NodePath(path_value))
	if node == null or (node != root and not root.is_ancestor_of(node)) or node == self:
		return ErrorEnvelope.failure(
			"Runtime node was not found", ErrorEnvelope.NOT_FOUND,
		)
	return _success(node)


func _node_metadata(root: Node, node: Node, depth: int) -> Dictionary:
	var path := _node_path(root, node)
	var parent_path: Variant = null
	if node != root:
		parent_path = _node_path(root, node.get_parent())
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
		"runtime_id": _runtime_id(node),
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
		_project_hash, _run_id, _debugger_session_id, _tree_generation, fingerprint,
	]).sha256_text()


func _hash_tree(
	root: Node, node: Node, hashing: HashingContext, state: Dictionary,
) -> void:
	if state.visited >= Limits.MAX_TREE_SCAN:
		return
	state.visited += 1
	hashing.update((JSON.stringify([
		_node_path(root, node), node.get_class(), _runtime_id(node),
	]) + "\n").to_utf8_buffer())
	for child in node.get_children():
		if child != self:
			_hash_tree(root, child, hashing, state)


func _runtime_id(node: Node) -> String:
	return JSON.stringify([
		_instance_nonce, _run_id, _debugger_session_id, node.get_instance_id(),
	]).sha256_text()


func _node_path(root: Node, node: Node) -> String:
	return "." if node == root else str(root.get_path_to(node)).left(512)


func _expected_snapshot(arguments: Dictionary) -> Dictionary:
	var snapshot = arguments.get("_expected_snapshot", "")
	if not snapshot is String:
		return _failure("Expected runtime snapshot is invalid")
	if not snapshot.is_empty() and (
		snapshot.length() != 64 or not snapshot.is_valid_hex_number()
	):
		return _failure("Expected runtime snapshot is invalid")
	return _success(snapshot)


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
