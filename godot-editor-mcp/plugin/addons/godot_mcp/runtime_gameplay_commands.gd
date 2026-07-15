extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")

var _gateway
var _run_id_provider: Callable


func _init(gateway, run_id_provider: Callable) -> void:
	_gateway = gateway
	_run_id_provider = run_id_provider


func handlers() -> Dictionary:
	return {
		"capture_game_view": Callable(self, "capture_game_view"),
		"send_input": Callable(self, "send_input"),
		"wait_runtime_condition": Callable(self, "wait_runtime_condition"),
	}


func capture_game_view(arguments: Dictionary) -> Dictionary:
	var valid := _validate_run(arguments)
	if not valid.ok:
		return valid
	return _gateway.begin_request("capture", arguments)


func send_input(arguments: Dictionary) -> Dictionary:
	var valid := _validate_run(arguments)
	if not valid.ok:
		return valid
	return _gateway.begin_request("input", arguments)


func wait_runtime_condition(arguments: Dictionary) -> Dictionary:
	var valid := _validate_run(arguments)
	if not valid.ok:
		return valid
	if arguments.get("scope") != "runtime":
		return ErrorEnvelope.failure(
			"Runtime condition scope must be runtime", ErrorEnvelope.INVALID_ARGUMENT,
		)
	if arguments.get("condition") == "play_state":
		return _gateway.begin_play_state_wait(arguments)
	return _gateway.begin_request("condition", arguments)


func _validate_run(arguments: Dictionary) -> Dictionary:
	var requested = arguments.get("run_id")
	if (
		(not requested is int and not requested is float)
		or float(requested) != floorf(float(requested))
		or int(requested) < 1
	):
		return ErrorEnvelope.failure(
			"run_id must be a positive integer", ErrorEnvelope.INVALID_ARGUMENT,
		)
	var active = _run_id_provider.call() if _run_id_provider.is_valid() else null
	if not active is int or int(active) < 1:
		return ErrorEnvelope.failure("No scene is running", ErrorEnvelope.NO_ACTIVE_RUN)
	if int(requested) != int(active):
		return ErrorEnvelope.failure(
			"Runtime request belongs to another run", ErrorEnvelope.STALE_RUNTIME_ID,
			{"active_run_id": active}, false,
		)
	arguments["run_id"] = int(requested)
	return ErrorEnvelope.success({"run_id": active})
