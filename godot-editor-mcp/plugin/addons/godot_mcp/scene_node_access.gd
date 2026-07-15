extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")

var _editor_interface: EditorInterface


func _init(editor_interface: EditorInterface) -> void:
	_editor_interface = editor_interface


func find(path_value: Variant) -> Dictionary:
	if not path_value is String or path_value.is_empty() or path_value.length() > 512:
		return ErrorEnvelope.failure("Path must be a non-empty string up to 512 characters")
	if path_value.begins_with("/") or ".." in path_value.split("/"):
		return ErrorEnvelope.failure("Path must be relative and cannot contain ..")
	var root := _editor_interface.get_edited_scene_root()
	if root == null:
		return ErrorEnvelope.failure("No scene is open")
	var node: Node = root if path_value == "." else root.get_node_or_null(NodePath(path_value))
	if node == null or (node != root and not root.is_ancestor_of(node)):
		return ErrorEnvelope.failure("Node not found")
	return ErrorEnvelope.success(node)


func checked_name(value: Variant) -> Dictionary:
	if not value is String or value.is_empty() or value.length() > 128:
		return ErrorEnvelope.failure("Node name must be a non-empty string up to 128 characters")
	if value.validate_node_name() != value:
		return ErrorEnvelope.failure("Node name contains invalid characters")
	return ErrorEnvelope.success(value)
