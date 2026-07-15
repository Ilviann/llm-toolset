extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")

var _events: RefCounted
var _operations: RefCounted
var _diagnostics: RefCounted
var _scene_state: RefCounted
var _run_state: RefCounted
var _import_state: RefCounted
var _project_file_state: RefCounted


func _init(
	events: RefCounted,
	operations: RefCounted,
	diagnostics: RefCounted,
	scene_state: RefCounted,
	run_state: RefCounted,
	import_state: RefCounted,
	project_file_state: RefCounted,
) -> void:
	_events = events
	_operations = operations
	_diagnostics = diagnostics
	_scene_state = scene_state
	_run_state = run_state
	_import_state = import_state
	_project_file_state = project_file_state


func stop() -> void:
	_import_state.stop()
	_run_state.stop()


func poll() -> void:
	_run_state.poll()
	_scene_state.poll()
	_import_state.poll()
	_project_file_state.poll()


func state() -> Dictionary:
	var output := {
		"godot": str(Engine.get_version_info().get("string", "Godot 4")),
		"project_name": str(ProjectSettings.get_setting("application/config/name", "")),
		"project_path": ProjectSettings.globalize_path("res://"),
		"main_scene": str(ProjectSettings.get_setting("application/run/main_scene", "")),
		"last_event_id": _events.latest_id(),
		"last_diagnostic_id": _diagnostics.latest_id(),
		"active_operations": _operations.concise_active(),
	}
	output.merge(_scene_state.state())
	output.merge(_run_state.state())
	output.merge(_import_state.state())
	output.merge(_project_file_state.state())
	return output


func scene_control(arguments: Dictionary) -> Dictionary:
	match arguments.get("action"):
		"save":
			return _scene_state.save()
		"run":
			return _run_state.run()
		"stop":
			return _run_state.stop_run(arguments)
		_:
			return ErrorEnvelope.failure(
				"Action must be save, run, or stop", ErrorEnvelope.INVALID_ARGUMENT,
			)
