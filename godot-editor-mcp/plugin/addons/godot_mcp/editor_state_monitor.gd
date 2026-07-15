extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")
const MAX_RECENT_IMPORTS := 16

var _editor_interface: EditorInterface
var _undo_redo: EditorUndoRedoManager
var _events: RefCounted
var _operations: RefCounted
var _diagnostics: RefCounted
var _filesystem_generation := 0
var _run_id := 0
var _was_playing := false
var _was_scanning := false
var _last_run_exit_status := "never_started"
var _last_stop_reason := ""
var _last_filesystem_event_id: Variant = null
var _last_run_event_id: Variant = null
var _last_scene_event_id: Variant = null
var _last_scene := ""
var _run_operation_id := ""
var _stop_operation_id := ""
var _pending_imports: Dictionary = {}
var _recent_imports: Array[Dictionary] = []
var _saved_history_version := 0
var _scene_modified_time := 0
var _project_file_hash := ""
var _known_project_file_hash := ""
var _project_reload_required := false
var _next_project_file_hash_check_ms := 0


func _init(
	editor_interface: EditorInterface,
	undo_redo: EditorUndoRedoManager,
	events: RefCounted,
	operations: RefCounted,
	diagnostics: RefCounted,
) -> void:
	_editor_interface = editor_interface
	_undo_redo = undo_redo
	_events = events
	_operations = operations
	_diagnostics = diagnostics
	_was_playing = _editor_interface.is_playing_scene()
	if _was_playing:
		_run_id = 1
		_last_run_exit_status = "running"
		_diagnostics.set_run_id(_run_id)
	var root := _editor_interface.get_edited_scene_root()
	_last_scene = "" if root == null else root.scene_file_path
	_reset_scene_baseline(root)
	var filesystem := _editor_interface.get_resource_filesystem()
	_was_scanning = filesystem.is_scanning()
	if not filesystem.filesystem_changed.is_connected(_on_filesystem_changed):
		filesystem.filesystem_changed.connect(_on_filesystem_changed)
	if not filesystem.resources_reimported.is_connected(_on_resources_reimported):
		filesystem.resources_reimported.connect(_on_resources_reimported)
	_project_file_hash = _hash_project_file()
	_known_project_file_hash = _project_file_hash


func stop() -> void:
	var filesystem := _editor_interface.get_resource_filesystem()
	if filesystem.filesystem_changed.is_connected(_on_filesystem_changed):
		filesystem.filesystem_changed.disconnect(_on_filesystem_changed)
	if filesystem.resources_reimported.is_connected(_on_resources_reimported):
		filesystem.resources_reimported.disconnect(_on_resources_reimported)
	_diagnostics.set_run_id(null)


func poll() -> void:
	var playing := _editor_interface.is_playing_scene()
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

	var root := _editor_interface.get_edited_scene_root()
	var scene := "" if root == null else root.scene_file_path
	if scene != _last_scene:
		_last_scene = scene
		_reset_scene_baseline(root)
		_last_scene_event_id = _events.append("scene_changed", {"scene": scene})
		_operations.complete_kind("open_scene", {"event_id": _last_scene_event_id})
	else:
		_detect_scene_save(root)

	var filesystem := _editor_interface.get_resource_filesystem()
	var scanning := filesystem.is_scanning()
	if _was_scanning and not scanning:
		_finish_pending_imports()
	_was_scanning = scanning
	if Time.get_ticks_msec() >= _next_project_file_hash_check_ms:
		_check_project_file_hash()
		_next_project_file_hash_check_ms = Time.get_ticks_msec() + 1000


func state() -> Dictionary:
	_check_project_file_hash()
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
	var scanning := filesystem.is_scanning()
	var progress := 0.0
	if scanning and filesystem.has_method("get_scanning_progress"):
		progress = clampf(float(filesystem.call("get_scanning_progress")), 0.0, 1.0)
	elif not scanning:
		progress = 1.0
	var run_counts: Dictionary = _diagnostics.counts(_run_id) if playing else {"errors": 0, "warnings": 0}
	return {
		"godot": str(Engine.get_version_info().get("string", "Godot 4")),
		"project_name": str(ProjectSettings.get_setting("application/config/name", "")),
		"project_path": ProjectSettings.globalize_path("res://"),
		"main_scene": str(ProjectSettings.get_setting("application/run/main_scene", "")),
		"scene": "" if root == null else root.scene_file_path,
		"root": "" if root == null else root.name,
		"scene_dirty": _scene_is_dirty(root),
		"selected": selected,
		"playing": playing,
		"filesystem_scanning": scanning,
		"filesystem_phase": "scanning" if scanning else "idle",
		"filesystem_progress": progress,
		"filesystem_generation": _filesystem_generation,
		"active_imports": _active_imports(),
		"recent_imports": _recent_imports.duplicate(true),
		"import_errors": _recent_import_errors(),
		"run_id": _run_id if playing else null,
		"last_run_id": _run_id if _run_id > 0 else null,
		"last_run_exit_status": _last_run_exit_status,
		"last_stop_reason": _last_stop_reason,
		"current_run_diagnostic_counts": run_counts,
		"project_file_hash": _project_file_hash,
		"project_file_hash_matches_known_write": _project_file_hash == _known_project_file_hash,
		"project_reload_required": _project_reload_required,
		"last_event_id": _events.latest_id(),
		"last_diagnostic_id": _diagnostics.latest_id(),
		"filesystem_event_id": _last_filesystem_event_id,
		"scene_event_id": _last_scene_event_id,
		"run_event_id": _last_run_event_id,
		"active_operations": _operations.concise_active(),
	}


func track_import(path: String, operation_id: Variant) -> void:
	if operation_id == null:
		return
	_pending_imports[operation_id] = {
		"operation_id": operation_id,
		"path": path,
		"status": "active",
		"diagnostic_since": 0 if _diagnostics.latest_id() == null else _diagnostics.latest_id(),
	}


func mark_project_settings_saved(reload_required: bool) -> void:
	_project_file_hash = _hash_project_file()
	_known_project_file_hash = _project_file_hash
	_project_reload_required = _project_reload_required or reload_required


func mark_scene_saved() -> void:
	_reset_scene_baseline(_editor_interface.get_edited_scene_root())


func scene_control(arguments: Dictionary) -> Dictionary:
	var action = arguments.get("action")
	match action:
		"save":
			if _editor_interface.get_edited_scene_root() == null:
				return ErrorEnvelope.failure("No scene is open", ErrorEnvelope.NOT_FOUND)
			_editor_interface.save_scene()
			mark_scene_saved()
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
			_diagnostics.set_run_id(_run_id)
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
	if not _editor_interface.get_resource_filesystem().is_scanning():
		_finish_pending_imports()


func _on_resources_reimported(paths: PackedStringArray) -> void:
	for path in paths:
		for operation_id in _pending_imports:
			if _pending_imports[operation_id].path == path:
				_complete_import(operation_id)


func _finish_pending_imports() -> void:
	for operation_id in _pending_imports.keys():
		_complete_import(operation_id)
	_operations.complete_kind("filesystem_scan", {"event_id": _last_filesystem_event_id})


func _complete_import(operation_id: String) -> void:
	if not _pending_imports.has(operation_id):
		return
	var record: Dictionary = _pending_imports[operation_id]
	var path: String = record.path
	var resource_type := _editor_interface.get_resource_filesystem().get_file_type(path)
	var loadable := ResourceLoader.exists(path)
	var error = _diagnostics.latest_error_for_path(path, int(record.diagnostic_since))
	var expects_resource := path.get_extension().to_lower() not in ["csv", "json"]
	if error != null:
		record.status = "failed"
		record.error = {
			"message": str(error.message).left(512),
			"event_id": error.event_id,
		}
	elif expects_resource and resource_type.is_empty() and not loadable:
		record.status = "failed"
		record.error = {"message": "Godot did not produce a loadable resource"}
	else:
		record.status = "completed"
	record["type"] = resource_type
	record["loadable"] = loadable
	record.erase("diagnostic_since")
	_recent_imports.push_front(record)
	while _recent_imports.size() > MAX_RECENT_IMPORTS:
		_recent_imports.pop_back()
	_operations.complete(operation_id, record)
	_pending_imports.erase(operation_id)


func _active_imports() -> Array[Dictionary]:
	var output: Array[Dictionary] = []
	for operation_id in _pending_imports:
		var record: Dictionary = _pending_imports[operation_id].duplicate(true)
		record.erase("diagnostic_since")
		output.append(record)
		if output.size() >= MAX_RECENT_IMPORTS:
			break
	return output


func _recent_import_errors() -> Array[Dictionary]:
	var output: Array[Dictionary] = []
	for record in _recent_imports:
		if record.status == "failed":
			output.append(record)
	return output


func _scene_history_version(root: Node) -> int:
	if root == null:
		return 0
	var history := _undo_redo.get_history_undo_redo(root.get_instance_id())
	return 0 if history == null else history.get_version()


func _scene_is_dirty(root: Node) -> bool:
	return root != null and _scene_history_version(root) != _saved_history_version


func _reset_scene_baseline(root: Node) -> void:
	_saved_history_version = _scene_history_version(root)
	_scene_modified_time = _scene_file_modified_time(root)


func _detect_scene_save(root: Node) -> void:
	var modified := _scene_file_modified_time(root)
	if modified > 0 and modified != _scene_modified_time:
		_scene_modified_time = modified
		_saved_history_version = _scene_history_version(root)


func _scene_file_modified_time(root: Node) -> int:
	if root == null or root.scene_file_path.is_empty():
		return 0
	return int(FileAccess.get_modified_time(root.scene_file_path))


func _hash_project_file() -> String:
	var bytes := FileAccess.get_file_as_bytes("res://project.godot")
	if bytes.is_empty():
		return ""
	var context := HashingContext.new()
	context.start(HashingContext.HASH_SHA256)
	context.update(bytes)
	return context.finish().hex_encode()


func _check_project_file_hash() -> void:
	var current := _hash_project_file()
	if current != _project_file_hash:
		_project_file_hash = current
		if current != _known_project_file_hash:
			_project_reload_required = true
