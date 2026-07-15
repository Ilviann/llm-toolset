extends RefCounted

const MAX_RECENT_IMPORTS := 16

var _editor_interface
var _events: RefCounted
var _operations: RefCounted
var _diagnostics: RefCounted
var _filesystem_generation := 0
var _was_scanning := false
var _last_filesystem_event_id: Variant = null
var _pending_imports: Dictionary = {}
var _recent_imports: Array[Dictionary] = []


func _init(
	editor_interface, events: RefCounted, operations: RefCounted, diagnostics: RefCounted,
) -> void:
	_editor_interface = editor_interface
	_events = events
	_operations = operations
	_diagnostics = diagnostics
	var filesystem = _editor_interface.get_resource_filesystem()
	_was_scanning = filesystem.is_scanning()
	if not filesystem.filesystem_changed.is_connected(_on_filesystem_changed):
		filesystem.filesystem_changed.connect(_on_filesystem_changed)
	if not filesystem.resources_reimported.is_connected(_on_resources_reimported):
		filesystem.resources_reimported.connect(_on_resources_reimported)


func stop() -> void:
	var filesystem = _editor_interface.get_resource_filesystem()
	if filesystem.filesystem_changed.is_connected(_on_filesystem_changed):
		filesystem.filesystem_changed.disconnect(_on_filesystem_changed)
	if filesystem.resources_reimported.is_connected(_on_resources_reimported):
		filesystem.resources_reimported.disconnect(_on_resources_reimported)


func poll() -> void:
	var scanning: bool = _editor_interface.get_resource_filesystem().is_scanning()
	if _was_scanning and not scanning:
		_finish_pending_imports()
	_was_scanning = scanning


func state() -> Dictionary:
	var filesystem = _editor_interface.get_resource_filesystem()
	var scanning: bool = filesystem.is_scanning()
	var progress := 0.0
	if scanning and filesystem.has_method("get_scanning_progress"):
		progress = clampf(float(filesystem.call("get_scanning_progress")), 0.0, 1.0)
	elif not scanning:
		progress = 1.0
	return {
		"filesystem_scanning": scanning,
		"filesystem_phase": "scanning" if scanning else "idle",
		"filesystem_progress": progress,
		"filesystem_generation": _filesystem_generation,
		"active_imports": _active_imports(),
		"recent_imports": _recent_imports.duplicate(true),
		"import_errors": _recent_import_errors(),
		"filesystem_event_id": _last_filesystem_event_id,
	}


func track_import(path: String, operation_id: Variant) -> void:
	if not operation_id is String or operation_id.is_empty():
		return
	_pending_imports[operation_id] = {
		"operation_id": operation_id,
		"path": path,
		"status": "active",
		"diagnostic_since": 0 if _diagnostics.latest_id() == null else _diagnostics.latest_id(),
	}


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
	var filesystem = _editor_interface.get_resource_filesystem()
	var resource_type: String = filesystem.get_file_type(path)
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
