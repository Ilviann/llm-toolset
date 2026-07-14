extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")

var _editor_interface: EditorInterface
var _events: RefCounted
var _operations: RefCounted
var _filesystem_generation := 0
var _run_id := 0
var _was_playing := false
var _last_run_exit_status := "never_started"
var _last_stop_reason := ""
var _last_filesystem_event_id: Variant = null
var _last_run_event_id: Variant = null
var _last_scene_event_id: Variant = null
var _last_scene := ""
var _run_operation_id := ""
var _stop_operation_id := ""


func _init(editor_interface: EditorInterface, events: RefCounted, operations: RefCounted) -> void:
	_editor_interface = editor_interface
	_events = events
	_operations = operations
	_was_playing = _editor_interface.is_playing_scene()
	if _was_playing:
		_run_id = 1
		_last_run_exit_status = "running"
	var root := _editor_interface.get_edited_scene_root()
	_last_scene = "" if root == null else root.scene_file_path
	var filesystem := _editor_interface.get_resource_filesystem()
	if not filesystem.filesystem_changed.is_connected(_on_filesystem_changed):
		filesystem.filesystem_changed.connect(_on_filesystem_changed)


func stop() -> void:
	var filesystem := _editor_interface.get_resource_filesystem()
	if filesystem.filesystem_changed.is_connected(_on_filesystem_changed):
		filesystem.filesystem_changed.disconnect(_on_filesystem_changed)


func poll() -> void:
	var playing := _editor_interface.is_playing_scene()
	if playing and not _was_playing:
		if _run_operation_id.is_empty():
			_run_id += 1
		_last_run_exit_status = "running"
		_last_stop_reason = ""
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
	_was_playing = playing

	var root := _editor_interface.get_edited_scene_root()
	var scene := "" if root == null else root.scene_file_path
	if scene != _last_scene:
		_last_scene = scene
		_last_scene_event_id = _events.append("scene_changed", {"scene": scene})
		_operations.complete_kind("open_scene", {"event_id": _last_scene_event_id})


func state() -> Dictionary:
	var root := _editor_interface.get_edited_scene_root()
	var filesystem := _editor_interface.get_resource_filesystem()
	var playing := _editor_interface.is_playing_scene()
	var selected: Array[String] = []
	if root != null:
		for node in _editor_interface.get_selection().get_selected_nodes():
			if node == root:
				selected.append(".")
			elif root.is_ancestor_of(node):
				selected.append(str(root.get_path_to(node)))
	return {
		"godot": str(Engine.get_version_info().get("string", "Godot 4")),
		"project_name": str(ProjectSettings.get_setting("application/config/name", "")),
		"project_path": ProjectSettings.globalize_path("res://"),
		"main_scene": str(ProjectSettings.get_setting("application/run/main_scene", "")),
		"scene": "" if root == null else root.scene_file_path,
		"root": "" if root == null else root.name,
		"selected": selected,
		"playing": playing,
		"filesystem_scanning": filesystem.is_scanning(),
		"filesystem_generation": _filesystem_generation,
		"run_id": _run_id if playing else null,
		"last_run_id": _run_id if _run_id > 0 else null,
		"last_run_exit_status": _last_run_exit_status,
		"last_stop_reason": _last_stop_reason,
		"last_event_id": _events.latest_id(),
		"filesystem_event_id": _last_filesystem_event_id,
		"scene_event_id": _last_scene_event_id,
		"run_event_id": _last_run_event_id,
		"active_operations": _operations.concise_active(),
	}


func scene_control(arguments: Dictionary) -> Dictionary:
	var action = arguments.get("action")
	match action:
		"save":
			if _editor_interface.get_edited_scene_root() == null:
				return ErrorEnvelope.failure("No scene is open", ErrorEnvelope.NOT_FOUND)
			_editor_interface.save_scene()
			return ErrorEnvelope.success("Scene saved")
		"run":
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
			_editor_interface.play_current_scene()
			return ErrorEnvelope.success({
				"message": "Scene started",
				"run_id": _run_id,
				"operation_id": _run_operation_id,
			})
		"stop":
			if not _editor_interface.is_playing_scene():
				return ErrorEnvelope.failure("No scene is running", ErrorEnvelope.NO_ACTIVE_RUN)
			var requested_run_id = arguments.get("run_id")
			if not requested_run_id is int or requested_run_id < 1:
				return ErrorEnvelope.failure(
					"run_id is required to stop a scene", ErrorEnvelope.INVALID_ARGUMENT,
				)
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
		_:
			return ErrorEnvelope.failure(
				"Action must be save, run, or stop", ErrorEnvelope.INVALID_ARGUMENT,
			)


func _on_filesystem_changed() -> void:
	_filesystem_generation += 1
	_last_filesystem_event_id = _events.append(
		"filesystem_changed", {"generation": _filesystem_generation},
	)
	_operations.complete_kind(
		"filesystem_scan", {"event_id": _last_filesystem_event_id},
	)
