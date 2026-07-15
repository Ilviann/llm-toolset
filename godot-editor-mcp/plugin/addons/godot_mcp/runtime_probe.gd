extends Node

const ErrorEnvelope := preload("error_envelope.gd")
const Limits := preload("command_limits.gd")
const ProjectIdentity := preload("project_identity.gd")
const RuntimeCaptureService := preload("runtime_capture_service.gd")
const RuntimeConditionService := preload("runtime_condition_service.gd")
const RuntimeIdentityContext := preload("runtime_identity_context.gd")
const RuntimeInputService := preload("runtime_input_service.gd")
const RuntimeTreeService := preload("runtime_tree_service.gd")

const CAPTURE := "godot_mcp"
const PROBE_VERSION := "2"
const COMMANDS := ["capture", "condition", "input", "inspect", "tree"]
const HANDSHAKE_RETRY_MSEC := 250

var _registered := false
var _last_hello_msec := -HANDSHAKE_RETRY_MSEC
var _context
var _tree_service
var _capture_service
var _input_service
var _condition_service


func _init() -> void:
	_context = RuntimeIdentityContext.new(self)
	_tree_service = RuntimeTreeService.new(_context)
	_capture_service = RuntimeCaptureService.new(_context)
	_input_service = RuntimeInputService.new(_context)
	_condition_service = RuntimeConditionService.new(_context)


func _enter_tree() -> void:
	set_process(false)
	set_physics_process(false)
	if not EngineDebugger.is_active() or EngineDebugger.has_capture(CAPTURE):
		return
	_context.configure(
		ProjectIdentity.current_hash(), 0, 0,
		Crypto.new().generate_random_bytes(16).hex_encode(),
	)
	EngineDebugger.register_message_capture(CAPTURE, Callable(self, "_capture"))
	_registered = true
	set_process(true)
	_capture_service.cleanup_stale_captures()
	_send_hello()


func _process(_delta: float) -> void:
	if _registered and _context.run_id < 1:
		_send_hello()
		return
	_input_service.poll()
	_condition_service.poll(Callable(self, "_send_response"))


func _send_hello() -> void:
	var now := int(Time.get_ticks_msec())
	if now - _last_hello_msec < HANDSHAKE_RETRY_MSEC:
		return
	_last_hello_msec = now
	EngineDebugger.send_message(CAPTURE + ":hello", [{
		"project_hash": _context.project_hash,
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
		"instance_nonce": _context.instance_nonce,
	}])


func _exit_tree() -> void:
	_input_service.release_all("probe_shutdown")
	_condition_service.clear()
	if _registered and EngineDebugger.has_capture(CAPTURE):
		EngineDebugger.unregister_message_capture(CAPTURE)
	_registered = false
	_context.clear_run()


func _capture(message: String, data: Array) -> bool:
	if data.size() != 1 or not data[0] is Dictionary:
		return true
	var payload := data[0] as Dictionary
	match message:
		"accept":
			_accept(payload)
		"reject":
			_input_service.release_all("probe_rejected")
			_condition_service.clear()
			_context.clear_run()
			set_process(false)
		"request":
			_respond(payload)
	return true


func _accept(payload: Dictionary) -> void:
	if (
		payload.get("project_hash") != _context.project_hash
		or payload.get("probe_version") != PROBE_VERSION
		or payload.get("instance_nonce") != _context.instance_nonce
		or not payload.get("run_id") is int
		or int(payload.run_id) < 1
		or not payload.get("debugger_session_id") is int
		or int(payload.debugger_session_id) < 0
	):
		return
	_context.configure(
		_context.project_hash, int(payload.run_id), int(payload.debugger_session_id),
		_context.instance_nonce,
	)
	set_process(true)
	EngineDebugger.send_message(CAPTURE + ":handshake", [{
		"run_id": _context.run_id,
		"debugger_session_id": _context.debugger_session_id,
		"project_hash": _context.project_hash,
		"probe_version": PROBE_VERSION,
		"instance_nonce": _context.instance_nonce,
	}])


func _respond(payload: Dictionary) -> void:
	var request_id = payload.get("request_id")
	if not request_id is String or request_id.length() != 32:
		return
	var response: Dictionary
	if (
		_context.run_id < 1
		or payload.get("run_id") != _context.run_id
		or payload.get("debugger_session_id") != _context.debugger_session_id
		or payload.get("project_hash") != _context.project_hash
		or payload.get("probe_version") != PROBE_VERSION
	):
		response = ErrorEnvelope.failure(
			"Runtime request identity is stale", ErrorEnvelope.STALE_RUNTIME_ID,
		)
	else:
		var arguments = payload.get("arguments")
		if not arguments is Dictionary:
			response = ErrorEnvelope.failure("Runtime request arguments are invalid")
		else:
			match payload.get("command"):
				"tree":
					response = _tree_service.scene_tree(arguments)
				"inspect":
					response = _tree_service.inspect_node(arguments)
				"capture":
					response = _capture_service.capture_game_view(request_id, arguments)
				"input":
					response = _input_service.send_input(arguments)
				"condition":
					var condition_result: Dictionary = _condition_service.begin(request_id, arguments)
					if condition_result.pending:
						return
					response = condition_result.response
				_:
					response = ErrorEnvelope.failure("Runtime command is unsupported")
	_send_response(request_id, response)


func _send_response(request_id: String, response: Dictionary) -> void:
	var message := {
		"request_id": request_id,
		"run_id": _context.run_id,
		"debugger_session_id": _context.debugger_session_id,
		"project_hash": _context.project_hash,
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
