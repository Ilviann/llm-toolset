@tool
extends EditorDebuggerPlugin

const ErrorEnvelope := preload("error_envelope.gd")
const Limits := preload("command_limits.gd")

const CAPTURE := "godot_mcp"
const DEFERRED_RESPONSE_KEY := "__godot_mcp_deferred_response"
const REQUIRED_COMMANDS := ["inspect", "tree"]
const REQUIRED_LIMITS := {
	"tree_nodes": Limits.MAX_TREE_NODES,
	"tree_depth": Limits.MAX_TREE_DEPTH,
	"tree_scan": Limits.MAX_TREE_SCAN,
	"properties": Limits.MAX_PROPERTIES,
	"property_scan": Limits.MAX_PROPERTY_SCAN,
}

var _run_id_provider: Callable
var _project_hash := ""
var _probe_version := ""
var _session_provider: Callable
var _message_sender: Callable
var _clock: Callable
var _known_sessions: Dictionary = {}
var _handshakes: Dictionary = {}
var _pending: Dictionary = {}
var _ready: Dictionary = {}


func _init(
	run_id_provider: Callable,
	project_hash: String,
	probe_version: String,
	session_provider := Callable(),
	message_sender := Callable(),
	clock := Callable(),
) -> void:
	_run_id_provider = run_id_provider
	_project_hash = project_hash
	_probe_version = probe_version
	_session_provider = session_provider
	_message_sender = message_sender
	_clock = clock


func _has_capture(capture: String) -> bool:
	return capture == CAPTURE


func _setup_session(session_id: int) -> void:
	_known_sessions[session_id] = true
	if _session_provider.is_valid():
		return
	var session := get_session(session_id)
	if session == null:
		return
	var started := Callable(self, "_on_session_started").bind(session_id)
	var stopped := Callable(self, "_on_session_stopped").bind(session_id)
	if not session.started.is_connected(started):
		session.started.connect(started)
	if not session.stopped.is_connected(stopped):
		session.stopped.connect(stopped)
	if session.is_active():
		_on_session_started(session_id)


func _capture(message: String, data: Array, session_id: int) -> bool:
	if not message.begins_with(CAPTURE + ":"):
		return false
	var action := message.trim_prefix(CAPTURE + ":")
	if data.size() != 1 or not data[0] is Dictionary:
		return true
	var payload := data[0] as Dictionary
	match action:
		"hello":
			_accept_hello(session_id, payload)
		"handshake":
			_complete_handshake(session_id, payload)
		"response":
			_accept_response(session_id, payload)
	return true


func begin_request(
	command: String, arguments: Dictionary, completion := Callable(),
) -> Dictionary:
	_prune_ready()
	var run_id = _run_id_provider.call() if _run_id_provider.is_valid() else null
	if not run_id is int or run_id < 1:
		return ErrorEnvelope.failure(
			"No scene is running", ErrorEnvelope.NO_ACTIVE_RUN,
		)
	var active_sessions := _active_session_ids()
	if active_sessions.size() > 1:
		return ErrorEnvelope.failure(
			"More than one runtime debugger session is active",
			ErrorEnvelope.AMBIGUOUS_RUNTIME_SESSION,
			{"active_sessions": active_sessions.size()}, false,
		)
	if active_sessions.is_empty():
		return ErrorEnvelope.failure(
			"The runtime probe is unavailable",
			ErrorEnvelope.RUNTIME_PROBE_UNAVAILABLE, {}, true,
		)
	var session_id: int = active_sessions[0]
	var handshake: Dictionary = _handshakes.get(session_id, {})
	if handshake.get("state") != "ready":
		if handshake.has("error"):
			return handshake.error
		return ErrorEnvelope.failure(
			"The runtime probe has not completed its handshake",
			ErrorEnvelope.RUNTIME_PROBE_UNAVAILABLE,
			{"debugger_session_id": session_id}, true,
		)
	if int(handshake.get("run_id", 0)) != run_id:
		return ErrorEnvelope.failure(
			"Runtime debugger identity is stale", ErrorEnvelope.STALE_RUNTIME_ID,
			{"active_run_id": run_id}, false,
		)
	if _pending.size() >= Limits.MAX_RUNTIME_PENDING_REQUESTS:
		return ErrorEnvelope.failure(
			"Too many runtime inspection requests are pending",
			ErrorEnvelope.EDITOR_BUSY,
			{"limit": Limits.MAX_RUNTIME_PENDING_REQUESTS}, true,
		)
	var request_id := Crypto.new().generate_random_bytes(16).hex_encode()
	_pending[request_id] = {
		"session_id": session_id,
		"run_id": run_id,
		"deadline_msec": _now_msec() + Limits.RUNTIME_REQUEST_TIMEOUT_MSEC,
		"completion": completion,
	}
	_send(session_id, CAPTURE + ":request", [{
		"request_id": request_id,
		"run_id": run_id,
		"debugger_session_id": session_id,
		"project_hash": _project_hash,
		"probe_version": _probe_version,
		"command": command,
		"arguments": arguments,
	}])
	return {DEFERRED_RESPONSE_KEY: request_id}


func take_response(request_id: Variant) -> Variant:
	_prune_ready()
	if not request_id is String:
		return ErrorEnvelope.failure(
			"Runtime response identity is malformed", ErrorEnvelope.INVALID_ARGUMENT,
		)
	if _ready.has(request_id):
		var response: Dictionary = _ready[request_id].response
		_ready.erase(request_id)
		return response
	if not _pending.has(request_id):
		return ErrorEnvelope.failure(
			"Runtime response identity is stale", ErrorEnvelope.STALE_RUNTIME_ID,
		)
	var pending: Dictionary = _pending[request_id]
	if int(pending.deadline_msec) > _now_msec():
		return null
	_pending.erase(request_id)
	return ErrorEnvelope.failure(
		"Runtime inspection timed out", ErrorEnvelope.TIMEOUT,
		{"timeout_ms": Limits.RUNTIME_REQUEST_TIMEOUT_MSEC}, true,
	)


func stop() -> void:
	for request_id in _pending:
		_store_ready(request_id, ErrorEnvelope.failure(
			"Runtime debugger session stopped", ErrorEnvelope.STALE_RUNTIME_ID,
		))
	_pending.clear()
	_handshakes.clear()
	_known_sessions.clear()


func status() -> Dictionary:
	_prune_ready()
	var active := _active_session_ids()
	var states := {"accepting": 0, "ready": 0, "rejected": 0}
	for session_id in active:
		var state := str((_handshakes.get(session_id, {}) as Dictionary).get("state", ""))
		if states.has(state):
			states[state] += 1
	return {
		"probe_version": _probe_version,
		"supported_commands": REQUIRED_COMMANDS.duplicate(),
		"active_sessions": active.size(),
		"ready_sessions": _ready_session_count(active),
		"handshake_states": states,
	}


func _accept_hello(session_id: int, payload: Dictionary) -> void:
	_known_sessions[session_id] = true
	var failure := _validate_hello(payload)
	if not failure.is_empty():
		_handshakes[session_id] = {"state": "rejected", "error": failure}
		_send(session_id, CAPTURE + ":reject", [failure])
		return
	var run_id = _run_id_provider.call() if _run_id_provider.is_valid() else null
	if not run_id is int or run_id < 1:
		return
	var nonce := str(payload.instance_nonce)
	_handshakes[session_id] = {
		"state": "accepting", "run_id": run_id, "instance_nonce": nonce,
	}
	_send(session_id, CAPTURE + ":accept", [{
		"run_id": run_id,
		"debugger_session_id": session_id,
		"project_hash": _project_hash,
		"probe_version": _probe_version,
		"instance_nonce": nonce,
	}])


func _validate_hello(payload: Dictionary) -> Dictionary:
	if payload.get("project_hash") != _project_hash:
		return ErrorEnvelope.failure(
			"Runtime probe belongs to another project", ErrorEnvelope.PROJECT_MISMATCH,
		)
	if payload.get("probe_version") != _probe_version:
		return ErrorEnvelope.failure(
			"Runtime probe version is incompatible", ErrorEnvelope.VERSION_MISMATCH,
			{
				"expected_probe_version": _probe_version,
				"probe_version": payload.get("probe_version"),
			}, false,
		)
	var nonce = payload.get("instance_nonce")
	if not nonce is String or nonce.length() != 32 or not nonce.is_valid_hex_number():
		return ErrorEnvelope.failure(
			"Runtime probe identity is malformed", ErrorEnvelope.INVALID_ARGUMENT,
		)
	var commands = payload.get("commands")
	if not commands is Array or commands != REQUIRED_COMMANDS:
		return ErrorEnvelope.failure(
			"Runtime probe commands are incompatible", ErrorEnvelope.VERSION_MISMATCH,
		)
	if payload.get("limits") != REQUIRED_LIMITS:
		return ErrorEnvelope.failure(
			"Runtime probe limits are incompatible", ErrorEnvelope.VERSION_MISMATCH,
		)
	return {}


func _complete_handshake(session_id: int, payload: Dictionary) -> void:
	var handshake: Dictionary = _handshakes.get(session_id, {})
	if handshake.get("state") != "accepting":
		return
	if (
		payload.get("run_id") != handshake.run_id
		or payload.get("debugger_session_id") != session_id
		or payload.get("project_hash") != _project_hash
		or payload.get("probe_version") != _probe_version
		or payload.get("instance_nonce") != handshake.instance_nonce
	):
		_handshakes[session_id] = {
			"state": "rejected",
			"error": ErrorEnvelope.failure(
				"Runtime probe handshake identity is stale",
				ErrorEnvelope.STALE_RUNTIME_ID,
			),
		}
		return
	handshake.state = "ready"
	_handshakes[session_id] = handshake


func _accept_response(session_id: int, payload: Dictionary) -> void:
	var request_id = payload.get("request_id")
	if not request_id is String or not _pending.has(request_id):
		return
	var pending: Dictionary = _pending[request_id]
	if (
		pending.session_id != session_id
		or payload.get("run_id") != pending.run_id
		or payload.get("debugger_session_id") != session_id
		or payload.get("project_hash") != _project_hash
		or payload.get("probe_version") != _probe_version
	):
		_store_ready(request_id, ErrorEnvelope.failure(
			"Runtime response identity is stale", ErrorEnvelope.STALE_RUNTIME_ID,
		))
		_pending.erase(request_id)
		return
	var response = payload.get("response")
	if not response is Dictionary or not response.get("ok") is bool:
		response = ErrorEnvelope.failure(
			"Runtime probe returned an invalid response", ErrorEnvelope.INVALID_ARGUMENT,
		)
	var completion: Callable = pending.completion
	if completion.is_valid():
		response = completion.call(response)
	_store_ready(request_id, response)
	_pending.erase(request_id)


func _on_session_started(session_id: int) -> void:
	_handshakes.erase(session_id)


func _on_session_stopped(session_id: int) -> void:
	_handshakes.erase(session_id)
	for request_id in _pending.keys():
		var pending: Dictionary = _pending[request_id]
		if int(pending.session_id) == session_id:
			_store_ready(request_id, ErrorEnvelope.failure(
				"Runtime debugger session stopped", ErrorEnvelope.STALE_RUNTIME_ID,
			))
			_pending.erase(request_id)


func _active_session_ids() -> Array[int]:
	if _session_provider.is_valid():
		var provided: Array[int] = []
		provided.assign(_session_provider.call())
		return provided
	var output: Array[int] = []
	for session_id in _known_sessions:
		var session := get_session(int(session_id))
		if session != null and session.is_active():
			output.append(int(session_id))
	output.sort()
	return output


func _ready_session_count(active_sessions: Array[int]) -> int:
	var count := 0
	for session_id in active_sessions:
		if (_handshakes.get(session_id, {}) as Dictionary).get("state") == "ready":
			count += 1
	return count


func _send(session_id: int, message: String, data: Array) -> void:
	if _message_sender.is_valid():
		_message_sender.call(session_id, message, data)
		return
	var session := get_session(session_id)
	if session != null and session.is_active():
		session.send_message(message, data)


func _now_msec() -> int:
	return int(_clock.call()) if _clock.is_valid() else int(Time.get_ticks_msec())


func _store_ready(request_id: String, response: Dictionary) -> void:
	_prune_ready()
	while _ready.size() >= Limits.MAX_RUNTIME_PENDING_REQUESTS:
		var oldest_id := ""
		var oldest_deadline := 0
		for candidate in _ready:
			var deadline := int(_ready[candidate].deadline_msec)
			if oldest_id.is_empty() or deadline < oldest_deadline:
				oldest_id = str(candidate)
				oldest_deadline = deadline
		_ready.erase(oldest_id)
	_ready[request_id] = {
		"response": response,
		"deadline_msec": _now_msec() + Limits.RUNTIME_REQUEST_TIMEOUT_MSEC,
	}


func _prune_ready() -> void:
	var now := _now_msec()
	for request_id in _ready.keys():
		if int(_ready[request_id].deadline_msec) <= now:
			_ready.erase(request_id)
