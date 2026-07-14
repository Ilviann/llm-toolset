extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")

var _editor_interface: EditorInterface
var _undo_redo: EditorUndoRedoManager
var _operations: RefCounted
var _state_monitor: RefCounted


func _init(
	editor_interface: EditorInterface,
	undo_redo: EditorUndoRedoManager,
	operations: RefCounted = null,
	state_monitor: RefCounted = null,
) -> void:
	_editor_interface = editor_interface
	_undo_redo = undo_redo
	_operations = operations
	_state_monitor = state_monitor


func get_editor_interface() -> EditorInterface:
	return _editor_interface


func get_undo_redo() -> EditorUndoRedoManager:
	return _undo_redo


func _find_node(path_value: Variant) -> Dictionary:
	if not path_value is String or path_value.is_empty() or path_value.length() > 512:
		return _failure("Path must be a non-empty string up to 512 characters")
	if path_value.begins_with("/") or ".." in path_value.split("/"):
		return _failure("Path must be relative and cannot contain ..")
	var root := get_editor_interface().get_edited_scene_root()
	if root == null:
		return _failure("No scene is open")
	var node: Node = root if path_value == "." else root.get_node_or_null(NodePath(path_value))
	if node == null or (node != root and not root.is_ancestor_of(node)):
		return _failure("Node not found")
	return _success(node)


func _checked_node_name(value: Variant) -> Dictionary:
	if not value is String or value.is_empty() or value.length() > 128:
		return _failure("Node name must be a non-empty string up to 128 characters")
	if value.validate_node_name() != value:
		return _failure("Node name contains invalid characters")
	return _success(value)


func _project_path(
	path_value: Variant,
	writable := false,
	allowed_extensions := PackedStringArray(),
) -> Dictionary:
	if not path_value is String or path_value.is_empty() or path_value.length() > 512:
		return _failure("Project path must be a non-empty string up to 512 characters")
	var path := path_value as String
	if path == "." or path.begins_with("/") or path.begins_with("res://") or "\\" in path or "//" in path:
		return _failure("Project path must be relative and cannot contain res://, . or ..")
	var parts := path.split("/")
	if "." in parts or ".." in parts:
		return _failure("Project path must be relative and cannot contain res://, . or ..")
	if writable and parts[0].to_lower() in [".godot", "addons"]:
		return _failure("Destination is a protected project folder")
	if not allowed_extensions.is_empty() and path.get_extension().to_lower() not in allowed_extensions:
		return _failure("Project path has an unsupported extension")
	if _project_path_has_link(path):
		return _failure("Project path cannot contain symbolic links")
	return _success("res://" + path)


func _project_path_has_link(relative_path: String) -> bool:
	var current := "res://"
	for part in relative_path.split("/"):
		var directory := DirAccess.open(current)
		if directory != null and directory.is_link(part):
			return true
		current = current.path_join(part)
	return false


func _convert_value(value: Variant, target_type: int) -> Dictionary:
	if target_type == TYPE_INT and (value is int or value is float):
		return _success(int(value))
	if target_type == TYPE_FLOAT and (value is int or value is float):
		return _success(float(value))
	if target_type == TYPE_VECTOR2 and value is Array and value.size() == 2:
		return _success(Vector2(float(value[0]), float(value[1])))
	if target_type == TYPE_VECTOR2I and value is Array and value.size() == 2:
		return _success(Vector2i(int(value[0]), int(value[1])))
	if target_type == TYPE_VECTOR3 and value is Array and value.size() == 3:
		return _success(Vector3(float(value[0]), float(value[1]), float(value[2])))
	if target_type == TYPE_VECTOR3I and value is Array and value.size() == 3:
		return _success(Vector3i(int(value[0]), int(value[1]), int(value[2])))
	if target_type == TYPE_COLOR and value is Array and value.size() in [3, 4]:
		return _success(Color(float(value[0]), float(value[1]), float(value[2]), float(value[3]) if value.size() == 4 else 1.0))
	if target_type == TYPE_NODE_PATH and value is String:
		return _success(NodePath(value))
	if target_type == TYPE_STRING_NAME and value is String:
		return _success(StringName(value))
	if target_type == typeof(value):
		return _success(value)
	return _failure("Value does not match property type %s" % type_string(target_type))


func _encode_value(value: Variant, depth := 0) -> Variant:
	if depth >= 3:
		return "..."
	match typeof(value):
		TYPE_NIL, TYPE_BOOL, TYPE_INT, TYPE_FLOAT:
			return value
		TYPE_STRING, TYPE_STRING_NAME, TYPE_NODE_PATH:
			var text := str(value)
			return text.left(512) + ("..." if text.length() > 512 else "")
		TYPE_VECTOR2, TYPE_VECTOR2I:
			return [value.x, value.y]
		TYPE_VECTOR3, TYPE_VECTOR3I:
			return [value.x, value.y, value.z]
		TYPE_COLOR:
			return [value.r, value.g, value.b, value.a]
		TYPE_ARRAY:
			var array: Array = []
			for item in value.slice(0, 20):
				array.append(_encode_value(item, depth + 1))
			return array
		TYPE_DICTIONARY:
			var dictionary := {}
			for key in value.keys().slice(0, 20):
				dictionary[str(key)] = _encode_value(value[key], depth + 1)
			return dictionary
		TYPE_OBJECT:
			if value == null:
				return null
			if value is Resource and not value.resource_path.is_empty():
				return value.resource_path
			return "<%s>" % value.get_class()
		_:
			return str(value).left(512)


func _only_keys(dictionary: Dictionary, allowed: Array) -> bool:
	for key in dictionary:
		if key not in allowed:
			return false
	return true


func _accept_operation(kind: String, details: Dictionary = {}, run_id: Variant = null) -> Variant:
	return null if _operations == null else _operations.accept(kind, details, run_id)


func _error_message(response: Dictionary) -> String:
	return ErrorEnvelope.message(response)


func _normalize_input_event(event: Variant) -> Dictionary:
	if event is InputEventKey:
		var physical: bool = event.physical_keycode != 0
		var code: int = event.physical_keycode if physical else event.keycode
		return {
			"type": "key", "key": OS.get_keycode_string(code),
			"physical": physical, "device": event.device,
			"shift": event.shift_pressed, "alt": event.alt_pressed,
			"ctrl": event.ctrl_pressed, "meta": event.meta_pressed,
		}
	if event is InputEventMouseButton:
		return {"type": "mouse_button", "button": int(event.button_index), "device": event.device}
	if event is InputEventJoypadButton:
		return {"type": "joypad_button", "button": int(event.button_index), "device": event.device}
	if event is InputEventJoypadMotion:
		return {
			"type": "joypad_motion", "axis": int(event.axis),
			"direction": -1 if event.axis_value < 0 else 1, "device": event.device,
		}
	return {"type": "unsupported", "class": event.get_class() if event is Object else type_string(typeof(event))}


func _success(result: Variant) -> Dictionary:
	return ErrorEnvelope.success(result)


func _failure(
	message: String,
	code := "",
	details: Variant = {},
	retryable := false,
) -> Dictionary:
	return ErrorEnvelope.failure(message, code, details, retryable)
