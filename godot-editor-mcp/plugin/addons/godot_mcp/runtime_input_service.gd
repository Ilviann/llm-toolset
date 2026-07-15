extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")
const Limits := preload("command_limits.gd")

var _context
var active_inputs: Dictionary = {}


func _init(context) -> void:
	_context = context


func send_input(arguments: Dictionary) -> Dictionary:
	var run_check: Dictionary = _context.requested_run(arguments)
	if not run_check.ok:
		return run_check
	var action = arguments.get("action")
	if not action is String or action.is_empty() or action.length() > 128:
		return _context.failure("Input action must be a string up to 128 characters")
	if not InputMap.has_action(StringName(action)):
		return ErrorEnvelope.failure(
			"Input Map action was not found", ErrorEnvelope.NOT_FOUND,
			{"action": action}, false,
		)
	var pressed = arguments.get("pressed", true)
	if not pressed is bool:
		return _context.failure("pressed must be a boolean")
	if not pressed:
		Input.action_release(StringName(action))
		active_inputs.erase(action)
		_log_injected_input(action, "released")
		return _context.success({
			"run_id": _context.run_id, "action": action, "pressed": false,
			"injected": true, "released": true,
		})
	var strength = arguments.get("strength", 1.0)
	if (
		(not strength is int and not strength is float)
		or not is_finite(float(strength))
		or float(strength) < 0.0
		or float(strength) > 1.0
	):
		return _context.failure("Input strength must be a finite number from 0 to 1")
	var has_duration := arguments.has("duration_ms")
	var has_frames := arguments.has("frames")
	if has_duration == has_frames:
		return _context.failure("Pressed input requires exactly one of duration_ms or frames")
	if active_inputs.has(action):
		return ErrorEnvelope.failure(
			"The input action is already injected", ErrorEnvelope.EDITOR_BUSY,
			{"action": action}, true,
		)
	if active_inputs.size() >= Limits.MAX_CONCURRENT_INPUTS:
		return ErrorEnvelope.failure(
			"Too many injected inputs are active", ErrorEnvelope.EDITOR_BUSY,
			{"limit": Limits.MAX_CONCURRENT_INPUTS}, true,
		)
	var release_msec := 0
	var release_frame := 0
	if has_duration:
		var duration_result: Dictionary = _context.bounded_integer(
			arguments.duration_ms, "Input duration", 1, Limits.MAX_INPUT_DURATION_MSEC,
		)
		if not duration_result.ok:
			return duration_result
		release_msec = int(Time.get_ticks_msec()) + int(duration_result.result)
	else:
		var frame_result: Dictionary = _context.bounded_integer(
			arguments.frames, "Input frames", 1, Limits.MAX_INPUT_FRAMES,
		)
		if not frame_result.ok:
			return frame_result
		release_frame = int(Engine.get_process_frames()) + int(frame_result.result)
	Input.action_press(StringName(action), float(strength))
	active_inputs[action] = {
		"release_msec": release_msec, "release_frame": release_frame,
	}
	_log_injected_input(action, "pressed")
	return _context.success({
		"run_id": _context.run_id,
		"action": action,
		"pressed": true,
		"strength": float(strength),
		"injected": true,
		"release_scheduled": true,
		"duration_ms": arguments.get("duration_ms"),
		"frames": arguments.get("frames"),
	})


func poll() -> void:
	var now := int(Time.get_ticks_msec())
	var frame := int(Engine.get_process_frames())
	for action in active_inputs.keys():
		var hold: Dictionary = active_inputs[action]
		if (
			(int(hold.release_msec) > 0 and now >= int(hold.release_msec))
			or (int(hold.release_frame) > 0 and frame >= int(hold.release_frame))
		):
			Input.action_release(StringName(action))
			active_inputs.erase(action)
			_log_injected_input(str(action), "auto_released")


func release_all(reason: String) -> void:
	for action in active_inputs.keys():
		Input.action_release(StringName(action))
		_log_injected_input(str(action), reason)
	active_inputs.clear()


func _log_injected_input(action: String, state: String) -> void:
	print("[GodotMCPInjectedInput] action=%s state=%s" % [JSON.stringify(action), state])
