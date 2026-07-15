extends Node

const ErrorEnvelope := preload("error_envelope.gd")
const Limits := preload("command_limits.gd")
const ProjectIdentity := preload("project_identity.gd")
const PropertyValueCodec := preload("property_value_codec.gd")

const CAPTURE := "godot_mcp"
const PROBE_VERSION := "2"
const COMMANDS := ["capture", "condition", "input", "inspect", "tree"]
const HANDSHAKE_RETRY_MSEC := 250
const CAPTURE_FOLDER := "res://.godot/godot_mcp/captures"
const CAPTURE_TTL_SECONDS := 120

var _registered := false
var _run_id := 0
var _debugger_session_id := 0
var _project_hash := ""
var _instance_nonce := ""
var _tree_fingerprint := ""
var _tree_generation := 0
var _last_hello_msec := -HANDSHAKE_RETRY_MSEC
var _property_values := PropertyValueCodec.new()
var _active_inputs: Dictionary = {}
var _pending_conditions: Dictionary = {}


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
	_cleanup_stale_captures()
	_send_hello()


func _process(_delta: float) -> void:
	if _registered and _run_id < 1:
		_send_hello()
		return
	_poll_inputs()
	_poll_conditions()


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
			"capture_source_width": Limits.MAX_CAPTURE_SOURCE_WIDTH,
			"capture_source_height": Limits.MAX_CAPTURE_SOURCE_HEIGHT,
			"capture_source_pixels": Limits.MAX_CAPTURE_SOURCE_PIXELS,
			"capture_output_width": Limits.MAX_CAPTURE_OUTPUT_WIDTH,
			"capture_output_height": Limits.MAX_CAPTURE_OUTPUT_HEIGHT,
			"capture_output_pixels": Limits.MAX_CAPTURE_OUTPUT_PIXELS,
			"capture_bytes": Limits.MAX_CAPTURE_BYTES,
			"capture_timeout_ms": Limits.CAPTURE_TIMEOUT_MSEC,
			"concurrent_inputs": Limits.MAX_CONCURRENT_INPUTS,
			"input_duration_ms": Limits.MAX_INPUT_DURATION_MSEC,
			"input_frames": Limits.MAX_INPUT_FRAMES,
			"condition_timeout_ms": Limits.MAX_CONDITION_TIMEOUT_MSEC,
			"condition_evidence": Limits.MAX_CONDITION_EVIDENCE,
		},
		"instance_nonce": _instance_nonce,
	}])


func _exit_tree() -> void:
	_release_all_inputs("probe_shutdown")
	_pending_conditions.clear()
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
			_release_all_inputs("probe_rejected")
			_pending_conditions.clear()
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
	set_process(true)
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
			response = _failure("Runtime request arguments are invalid")
		elif payload.get("command") == "tree":
			response = _scene_tree(arguments)
		elif payload.get("command") == "inspect":
			response = _inspect_node(arguments)
		elif payload.get("command") == "capture":
			response = _capture_game_view(request_id, arguments)
		elif payload.get("command") == "input":
			response = _send_input(arguments)
		elif payload.get("command") == "condition":
			if _begin_condition(request_id, arguments):
				return
			response = _pending_conditions.get(request_id, {}).get("response", {})
			_pending_conditions.erase(request_id)
		else:
			response = _failure("Runtime command is unsupported")
	_send_response(request_id, response)


func _send_response(request_id: String, response: Dictionary) -> void:
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
			"Runtime response is too large",
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


func _capture_game_view(request_id: String, arguments: Dictionary) -> Dictionary:
	var run_check := _requested_run(arguments)
	if not run_check.ok:
		return run_check
	var width_result := _bounded_integer(
		arguments.get("max_width", 1280), "Maximum capture width", 1,
		Limits.MAX_CAPTURE_OUTPUT_WIDTH,
	)
	if not width_result.ok:
		return width_result
	var height_result := _bounded_integer(
		arguments.get("max_height", 720), "Maximum capture height", 1,
		Limits.MAX_CAPTURE_OUTPUT_HEIGHT,
	)
	if not height_result.ok:
		return height_result
	var started := int(Time.get_ticks_msec())
	if DisplayServer.get_name() == "headless":
		return ErrorEnvelope.failure(
			"Game-view capture is unavailable with the headless display server",
			ErrorEnvelope.UNSUPPORTED_CAPABILITY, {}, false,
		)
	var viewport := get_tree().root
	if viewport == null:
		return ErrorEnvelope.failure(
			"The game viewport is unavailable", ErrorEnvelope.UNSUPPORTED_CAPABILITY,
		)
	var image: Image = viewport.get_texture().get_image()
	if image == null or image.is_empty():
		return ErrorEnvelope.failure(
			"The renderer did not provide a game viewport image",
			ErrorEnvelope.UNSUPPORTED_CAPABILITY, {}, true,
		)
	var source_width := image.get_width()
	var source_height := image.get_height()
	if (
		source_width < 1
		or source_height < 1
		or source_width > Limits.MAX_CAPTURE_SOURCE_WIDTH
		or source_height > Limits.MAX_CAPTURE_SOURCE_HEIGHT
		or source_width * source_height > Limits.MAX_CAPTURE_SOURCE_PIXELS
	):
		return ErrorEnvelope.failure(
			"The source viewport exceeds capture limits", ErrorEnvelope.INVALID_ARGUMENT,
			{"width": source_width, "height": source_height}, false,
		)
	var scale := minf(
		1.0,
		minf(
			float(width_result.result) / float(source_width),
			float(height_result.result) / float(source_height),
		),
	)
	var output_width := maxi(1, int(floorf(float(source_width) * scale)))
	var output_height := maxi(1, int(floorf(float(source_height) * scale)))
	if output_width * output_height > Limits.MAX_CAPTURE_OUTPUT_PIXELS:
		return _failure("Capture output pixel limit was exceeded")
	if output_width != source_width or output_height != source_height:
		image.resize(output_width, output_height, Image.INTERPOLATE_LANCZOS)
	var folder := ProjectSettings.globalize_path(CAPTURE_FOLDER)
	var directory_error := DirAccess.make_dir_recursive_absolute(folder)
	if directory_error != OK and directory_error != ERR_ALREADY_EXISTS:
		return ErrorEnvelope.failure(
			"Could not create the capture staging folder", ErrorEnvelope.SAVE_FAILED,
		)
	var capture_path := folder.path_join(request_id + ".png")
	if FileAccess.file_exists(capture_path):
		return ErrorEnvelope.failure(
			"Capture staging identity already exists", ErrorEnvelope.STALE_OPERATION,
		)
	var save_error := image.save_png(capture_path)
	if save_error != OK:
		return ErrorEnvelope.failure(
			"Could not stage the game viewport capture", ErrorEnvelope.SAVE_FAILED,
			{"error": save_error}, false,
		)
	var encoded_bytes := FileAccess.get_file_as_bytes(capture_path).size()
	var elapsed := int(Time.get_ticks_msec()) - started
	if (
		encoded_bytes < 8
		or encoded_bytes > Limits.MAX_CAPTURE_BYTES
		or elapsed > Limits.CAPTURE_TIMEOUT_MSEC
	):
		DirAccess.remove_absolute(capture_path)
		return ErrorEnvelope.failure(
			"Game viewport capture exceeded its bounds",
			ErrorEnvelope.TIMEOUT if elapsed > Limits.CAPTURE_TIMEOUT_MSEC else ErrorEnvelope.INVALID_ARGUMENT,
			{"bytes": encoded_bytes, "elapsed_ms": elapsed}, false,
		)
	return _success({
		"capture_id": request_id,
		"run_id": _run_id,
		"source_width": source_width,
		"source_height": source_height,
		"width": output_width,
		"height": output_height,
		"bytes": encoded_bytes,
		"format": "png",
	})


func _send_input(arguments: Dictionary) -> Dictionary:
	var run_check := _requested_run(arguments)
	if not run_check.ok:
		return run_check
	var action = arguments.get("action")
	if not action is String or action.is_empty() or action.length() > 128:
		return _failure("Input action must be a string up to 128 characters")
	if not InputMap.has_action(StringName(action)):
		return ErrorEnvelope.failure(
			"Input Map action was not found", ErrorEnvelope.NOT_FOUND,
			{"action": action}, false,
		)
	var pressed = arguments.get("pressed", true)
	if not pressed is bool:
		return _failure("pressed must be a boolean")
	if not pressed:
		Input.action_release(StringName(action))
		_active_inputs.erase(action)
		_log_injected_input(action, "released")
		return _success({
			"run_id": _run_id, "action": action, "pressed": false,
			"injected": true, "released": true,
		})
	var strength = arguments.get("strength", 1.0)
	if (
		(not strength is int and not strength is float)
		or not is_finite(float(strength))
		or float(strength) < 0.0
		or float(strength) > 1.0
	):
		return _failure("Input strength must be a finite number from 0 to 1")
	var has_duration := arguments.has("duration_ms")
	var has_frames := arguments.has("frames")
	if has_duration == has_frames:
		return _failure("Pressed input requires exactly one of duration_ms or frames")
	if _active_inputs.has(action):
		return ErrorEnvelope.failure(
			"The input action is already injected", ErrorEnvelope.EDITOR_BUSY,
			{"action": action}, true,
		)
	if _active_inputs.size() >= Limits.MAX_CONCURRENT_INPUTS:
		return ErrorEnvelope.failure(
			"Too many injected inputs are active", ErrorEnvelope.EDITOR_BUSY,
			{"limit": Limits.MAX_CONCURRENT_INPUTS}, true,
		)
	var release_msec := 0
	var release_frame := 0
	if has_duration:
		var duration_result := _bounded_integer(
			arguments.duration_ms, "Input duration", 1, Limits.MAX_INPUT_DURATION_MSEC,
		)
		if not duration_result.ok:
			return duration_result
		release_msec = int(Time.get_ticks_msec()) + int(duration_result.result)
	else:
		var frame_result := _bounded_integer(
			arguments.frames, "Input frames", 1, Limits.MAX_INPUT_FRAMES,
		)
		if not frame_result.ok:
			return frame_result
		release_frame = int(Engine.get_process_frames()) + int(frame_result.result)
	Input.action_press(StringName(action), float(strength))
	_active_inputs[action] = {
		"release_msec": release_msec, "release_frame": release_frame,
	}
	_log_injected_input(action, "pressed")
	return _success({
		"run_id": _run_id,
		"action": action,
		"pressed": true,
		"strength": float(strength),
		"injected": true,
		"release_scheduled": true,
		"duration_ms": arguments.get("duration_ms"),
		"frames": arguments.get("frames"),
	})


func _poll_inputs() -> void:
	var now := int(Time.get_ticks_msec())
	var frame := int(Engine.get_process_frames())
	for action in _active_inputs.keys():
		var hold: Dictionary = _active_inputs[action]
		if (
			(int(hold.release_msec) > 0 and now >= int(hold.release_msec))
			or (int(hold.release_frame) > 0 and frame >= int(hold.release_frame))
		):
			Input.action_release(StringName(action))
			_active_inputs.erase(action)
			_log_injected_input(str(action), "auto_released")


func _release_all_inputs(reason: String) -> void:
	for action in _active_inputs.keys():
		Input.action_release(StringName(action))
		_log_injected_input(str(action), reason)
	_active_inputs.clear()


func _log_injected_input(action: String, state: String) -> void:
	print("[GodotMCPInjectedInput] action=%s state=%s" % [JSON.stringify(action), state])


func _begin_condition(request_id: String, arguments: Dictionary) -> bool:
	var validation := _validate_condition(arguments)
	if not validation.ok:
		_pending_conditions[request_id] = {"response": validation}
		return false
	var evaluation := _evaluate_condition(arguments)
	if not evaluation.ok or bool(evaluation.result.get("matched", false)):
		_pending_conditions[request_id] = {"response": evaluation}
		return false
	_pending_conditions[request_id] = {
		"arguments": arguments.duplicate(true),
		"deadline_msec": int(Time.get_ticks_msec()) + int(arguments.get("timeout_ms", 1000)),
		"last_evidence": evaluation.result.get("evidence", {}),
	}
	return true


func _poll_conditions() -> void:
	var now := int(Time.get_ticks_msec())
	for request_id in _pending_conditions.keys():
		var pending: Dictionary = _pending_conditions[request_id]
		if not pending.has("arguments"):
			continue
		var evaluation := _evaluate_condition(pending.arguments)
		if not evaluation.ok or bool(evaluation.result.get("matched", false)):
			_pending_conditions.erase(request_id)
			_send_response(request_id, evaluation)
			continue
		pending.last_evidence = evaluation.result.get("evidence", {})
		if now >= int(pending.deadline_msec):
			_pending_conditions.erase(request_id)
			_send_response(request_id, ErrorEnvelope.failure(
				"Runtime condition timed out", ErrorEnvelope.TIMEOUT,
				{"condition": pending.arguments.get("condition"), "evidence": pending.last_evidence},
				true,
			))


func _validate_condition(arguments: Dictionary) -> Dictionary:
	var run_check := _requested_run(arguments)
	if not run_check.ok:
		return run_check
	if arguments.get("scope") != "runtime":
		return _failure("Runtime condition scope must be runtime")
	var condition = arguments.get("condition")
	if condition not in ["node_exists", "node_count", "property"]:
		return _failure("Runtime condition type is unsupported")
	var timeout_result := _bounded_integer(
		arguments.get("timeout_ms", 1000), "Condition timeout", 1,
		Limits.MAX_CONDITION_TIMEOUT_MSEC,
	)
	if not timeout_result.ok:
		return timeout_result
	var path_result := _validate_runtime_path(arguments.get("path", "."))
	if not path_result.ok:
		return path_result
	if condition == "node_exists":
		if not arguments.get("exists", true) is bool:
			return _failure("exists must be a boolean")
		return _success({})
	if condition == "node_count":
		var depth_result := _bounded_integer(
			arguments.get("max_depth", Limits.MAX_TREE_DEPTH), "Maximum depth", 0,
			Limits.MAX_TREE_DEPTH,
		)
		if not depth_result.ok:
			return depth_result
		if arguments.has("group"):
			var group = arguments.group
			if not group is String or group.is_empty() or group.length() > 128:
				return _failure("Group must be a string up to 128 characters")
		var count_value = arguments.get("value")
		if (
			(not count_value is int and not count_value is float)
			or float(count_value) != floorf(float(count_value))
			or int(count_value) < 0
		):
			return _failure("Node-count value must be a non-negative integer")
		arguments["value"] = int(count_value)
	else:
		var property_name = arguments.get("property")
		if not property_name is String or property_name.is_empty() or property_name.length() > 128:
			return _failure("Property condition requires a bounded property name")
		if not arguments.has("value") or not _is_scalar(arguments.value):
			return _failure("Property comparison value must be a bounded scalar")
	var comparison = arguments.get("comparison", "eq")
	if comparison not in ["eq", "ne", "lt", "lte", "gt", "gte"]:
		return _failure("Comparison is unsupported")
	return _success({})


func _evaluate_condition(arguments: Dictionary) -> Dictionary:
	var condition: String = arguments.condition
	if condition == "node_exists":
		var node := _condition_node(arguments.path if arguments.has("path") else ".")
		var actual := node != null
		var expected := bool(arguments.get("exists", true))
		return _condition_result(condition, actual == expected, {
			"path": arguments.get("path", "."), "exists": actual,
		})
	var resolved := _resolve_node(arguments.get("path", "."))
	if not resolved.ok:
		return resolved
	var target := resolved.result as Node
	if condition == "node_count":
		var state := {"visited": 0, "count": 0, "paths": []}
		_count_condition_nodes(
			get_tree().current_scene, target, 0, int(arguments.get("max_depth", Limits.MAX_TREE_DEPTH)),
			str(arguments.get("group", "")), state,
		)
		var matched := _compare_values(state.count, arguments.value, str(arguments.get("comparison", "eq")))
		return _condition_result(condition, matched, {
			"path": arguments.get("path", "."), "group": arguments.get("group"),
			"count": state.count, "visited": state.visited, "paths": state.paths,
		})
	var property_name: String = arguments.property
	if not _is_builtin_property(target, property_name):
		return ErrorEnvelope.failure(
			"Runtime conditions can read only built-in Godot properties",
			ErrorEnvelope.UNSUPPORTED_CAPABILITY, {"property": property_name}, false,
		)
	var actual_value = target.get(property_name)
	if not _is_scalar(actual_value):
		return ErrorEnvelope.failure(
			"Runtime property is not a bounded scalar",
			ErrorEnvelope.UNSUPPORTED_CAPABILITY, {"property": property_name}, false,
		)
	var comparison: String = arguments.get("comparison", "eq")
	var compared := _compare_values(actual_value, arguments.value, comparison)
	if compared == null:
		return _failure("Property values do not support the requested comparison")
	return _condition_result(condition, bool(compared), {
		"path": arguments.get("path", "."), "property": property_name,
		"actual": actual_value, "comparison": comparison,
	})


func _condition_result(condition: String, matched: bool, evidence: Dictionary) -> Dictionary:
	return _success({
		"scope": "runtime", "run_id": _run_id, "condition": condition,
		"matched": matched, "evidence": evidence,
	})


func _count_condition_nodes(
	root: Node, node: Node, depth: int, max_depth: int, group: String, state: Dictionary,
) -> void:
	if state.visited >= Limits.MAX_TREE_SCAN:
		return
	state.visited += 1
	if group.is_empty() or node.is_in_group(StringName(group)):
		state.count += 1
		if state.paths.size() < Limits.MAX_CONDITION_EVIDENCE:
			state.paths.append(_node_path(root, node))
	if depth >= max_depth:
		return
	for child in node.get_children():
		if child != self:
			_count_condition_nodes(root, child, depth + 1, max_depth, group, state)


func _compare_values(actual: Variant, expected: Variant, comparison: String) -> Variant:
	if comparison == "eq":
		return actual == expected
	if comparison == "ne":
		return actual != expected
	var both_numbers := (
		(actual is int or actual is float) and (expected is int or expected is float)
	)
	var both_strings := actual is String and expected is String
	if not both_numbers and not both_strings:
		return null
	match comparison:
		"lt": return actual < expected
		"lte": return actual <= expected
		"gt": return actual > expected
		"gte": return actual >= expected
	return null


func _is_builtin_property(node: Node, property_name: String) -> bool:
	for info in ClassDB.class_get_property_list(node.get_class()):
		if str(info.get("name", "")) == property_name:
			return true
	return false


func _is_scalar(value: Variant) -> bool:
	if value is String:
		return value.length() <= 512
	if value is float:
		return is_finite(value)
	return value == null or value is bool or value is int


func _requested_run(arguments: Dictionary) -> Dictionary:
	var requested = arguments.get("run_id")
	if (
		(not requested is int and not requested is float)
		or float(requested) != floorf(float(requested))
		or int(requested) < 1
	):
		return _failure("run_id must be a positive integer")
	if int(requested) != _run_id:
		return ErrorEnvelope.failure(
			"Runtime request belongs to another run", ErrorEnvelope.STALE_RUNTIME_ID,
			{"active_run_id": _run_id}, false,
		)
	return _success({"run_id": _run_id})


func _validate_runtime_path(path_value: Variant) -> Dictionary:
	if not path_value is String or path_value.is_empty() or path_value.length() > 512:
		return _failure("Runtime node path must be a string up to 512 characters")
	if (
		path_value.begins_with("/")
		or path_value == ".."
		or path_value.begins_with("../")
		or "/../" in path_value
	):
		return _failure("Runtime node path must remain inside the running scene")
	return _success(path_value)


func _condition_node(path_value: String) -> Node:
	var root := get_tree().current_scene
	if root == null:
		return null
	var node: Node = root if path_value == "." else root.get_node_or_null(NodePath(path_value))
	if node == null or node == self or (node != root and not root.is_ancestor_of(node)):
		return null
	return node


func _cleanup_stale_captures() -> void:
	var folder := ProjectSettings.globalize_path(CAPTURE_FOLDER)
	var directory := DirAccess.open(folder)
	if directory == null:
		return
	var cutoff := int(Time.get_unix_time_from_system()) - CAPTURE_TTL_SECONDS
	for file_name in directory.get_files():
		if file_name.ends_with(".png"):
			var path := folder.path_join(file_name)
			if int(FileAccess.get_modified_time(path)) < cutoff:
				DirAccess.remove_absolute(path)


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
