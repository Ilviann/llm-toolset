extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")

var _editor_interface
var _events: RefCounted
var _operations: RefCounted
var _diagnostics: RefCounted
var _run_id := 0
var _was_playing := false
var _last_run_exit_status := "never_started"
var _last_stop_reason := ""
var _last_run_event_id: Variant = null
var _run_operation_id := ""
var _stop_operation_id := ""


func _init(
	editor_interface, events: RefCounted, operations: RefCounted, diagnostics: RefCounted,
) -> void:
	_editor_interface = editor_interface
	_events = events
	_operations = operations
	_diagnostics = diagnostics
	_was_playing = _editor_interface.is_playing_scene()
	if _was_playing:
		_run_id = 1
		_last_run_exit_status = "running"
		_diagnostics.set_run_id(_run_id)


func stop() -> void:
	_diagnostics.set_run_id(null)


func poll() -> void:
	var playing: bool = _editor_interface.is_playing_scene()
	if playing and not _was_playing:
		if _run_operation_id.is_empty():
			_run_id += 1
		_last_run_exit_status = "running"
		_last_stop_reason = ""
		_diagnostics.set_run_id(_run_id)
		_last_run_event_id = _events.append("run_started", {}, _run_id)
		if not _run_operation_id.is_empty():
			_operations.complete(_run_operation_id, {"event_id": _last_run_event_id})
			_run_operation_id = ""
	elif not playing and _was_playing:
		_last_run_exit_status = "stopped"
		if _last_stop_reason.is_empty():
			_last_stop_reason = "run_ended"
		_last_run_event_id = _events.append(
			"run_stopped", {"reason": _last_stop_reason}, _run_id,
		)
		if not _stop_operation_id.is_empty():
			_operations.complete(_stop_operation_id, {"event_id": _last_run_event_id})
			_stop_operation_id = ""
		_diagnostics.set_run_id(null)
	_was_playing = playing


func state() -> Dictionary:
	var playing: bool = _editor_interface.is_playing_scene()
	var counts: Dictionary = (
		_diagnostics.counts(_run_id) if playing else {"errors": 0, "warnings": 0}
	)
	return {
		"playing": playing,
		"run_id": _run_id if playing else null,
		"last_run_id": _run_id if _run_id > 0 else null,
		"last_run_exit_status": _last_run_exit_status,
		"last_stop_reason": _last_stop_reason,
		"current_run_diagnostic_counts": counts,
		"run_event_id": _last_run_event_id,
	}


func current_run_id() -> Variant:
	return _run_id if _editor_interface.is_playing_scene() else null


func run() -> Dictionary:
	if _editor_interface.get_edited_scene_root() == null:
		return ErrorEnvelope.failure("No scene is open", ErrorEnvelope.NOT_FOUND)
	if _editor_interface.is_playing_scene():
		return ErrorEnvelope.failure(
			"A scene is already running", ErrorEnvelope.EDITOR_BUSY,
			{"run_id": _run_id}, false,
		)
	_run_id += 1
	_run_operation_id = _operations.accept("run_scene", {}, _run_id)
	_last_run_exit_status = "starting"
	_last_stop_reason = ""
	_diagnostics.set_run_id(_run_id)
	_editor_interface.play_current_scene()
	return ErrorEnvelope.success({
		"message": "Scene started",
		"run_id": _run_id,
		"operation_id": _run_operation_id,
	})


func stop_run(arguments: Dictionary) -> Dictionary:
	if not _editor_interface.is_playing_scene():
		return ErrorEnvelope.failure("No scene is running", ErrorEnvelope.NO_ACTIVE_RUN)
	var requested_run_id = arguments.get("run_id")
	if (
		(not requested_run_id is int and not requested_run_id is float)
		or float(requested_run_id) != floorf(float(requested_run_id))
		or int(requested_run_id) < 1
	):
		return ErrorEnvelope.failure(
			"run_id is required to stop a scene", ErrorEnvelope.INVALID_ARGUMENT,
		)
	requested_run_id = int(requested_run_id)
	if requested_run_id != _run_id:
		return ErrorEnvelope.failure(
			"Run ID is stale", ErrorEnvelope.STALE_RUNTIME_ID,
			{"active_run_id": _run_id, "requested_run_id": requested_run_id}, false,
		)
	_last_stop_reason = "requested"
	_stop_operation_id = _operations.accept("stop_scene", {}, _run_id)
	_editor_interface.stop_playing_scene()
	return ErrorEnvelope.success({
		"message": "Scene stopped",
		"run_id": _run_id,
		"operation_id": _stop_operation_id,
	})
