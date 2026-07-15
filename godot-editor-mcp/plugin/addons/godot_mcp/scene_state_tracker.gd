extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")

var _editor_interface
var _undo_redo
var _events: RefCounted
var _operations: RefCounted
var _last_scene := ""
var _last_scene_instance_id := 0
var _last_scene_event_id: Variant = null
var _saved_history_version := 0
var _scene_modified_time := 0


func _init(
	editor_interface, undo_redo, events: RefCounted, operations: RefCounted,
) -> void:
	_editor_interface = editor_interface
	_undo_redo = undo_redo
	_events = events
	_operations = operations
	var root = _editor_interface.get_edited_scene_root()
	_last_scene = "" if root == null else root.scene_file_path
	_last_scene_instance_id = 0 if root == null else root.get_instance_id()
	_reset_baseline(root)


func poll() -> void:
	var root = _editor_interface.get_edited_scene_root()
	var scene := "" if root == null else str(root.scene_file_path)
	var instance_id: int = 0 if root == null else root.get_instance_id()
	if scene != _last_scene or instance_id != _last_scene_instance_id:
		_last_scene = scene
		_last_scene_instance_id = instance_id
		_reset_baseline(root)
		_last_scene_event_id = _events.append("scene_changed", {"scene": scene})
		_operations.complete_kind("open_scene", {"event_id": _last_scene_event_id})
	else:
		_detect_save(root)


func state() -> Dictionary:
	var root = _editor_interface.get_edited_scene_root()
	var selected: Array[String] = []
	if root != null:
		for node in _editor_interface.get_selection().get_selected_nodes():
			if node == root:
				selected.append(".")
			elif root.is_ancestor_of(node):
				selected.append(str(root.get_path_to(node)))
	return {
		"scene": "" if root == null else root.scene_file_path,
		"root": "" if root == null else root.name,
		"scene_dirty": _is_dirty(root),
		"selected": selected,
		"scene_event_id": _last_scene_event_id,
	}


func save() -> Dictionary:
	if _editor_interface.get_edited_scene_root() == null:
		return ErrorEnvelope.failure("No scene is open", ErrorEnvelope.NOT_FOUND)
	_editor_interface.save_scene()
	mark_saved()
	return ErrorEnvelope.success("Scene saved")


func mark_saved() -> void:
	_reset_baseline(_editor_interface.get_edited_scene_root())


func _history_version(root) -> int:
	if root == null:
		return 0
	var history = _undo_redo.get_history_undo_redo(root.get_instance_id())
	return 0 if history == null else int(history.get_version())


func _is_dirty(root) -> bool:
	return root != null and _history_version(root) != _saved_history_version


func _reset_baseline(root) -> void:
	_saved_history_version = _history_version(root)
	_scene_modified_time = _file_modified_time(root)


func _detect_save(root) -> void:
	var modified := _file_modified_time(root)
	if modified > 0 and modified != _scene_modified_time:
		_scene_modified_time = modified
		_saved_history_version = _history_version(root)


func _file_modified_time(root) -> int:
	if root == null or root.scene_file_path.is_empty():
		return 0
	return int(FileAccess.get_modified_time(root.scene_file_path))
