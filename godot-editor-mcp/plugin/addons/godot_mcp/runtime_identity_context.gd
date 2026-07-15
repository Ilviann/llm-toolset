extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")

var project_hash := ""
var run_id := 0
var debugger_session_id := 0
var instance_nonce := ""
var ignored_node: Node


func _init(node: Node) -> void:
	ignored_node = node


func configure(
	selected_project_hash: String,
	selected_run_id: int,
	selected_debugger_session_id: int,
	selected_instance_nonce: String,
) -> void:
	project_hash = selected_project_hash
	run_id = selected_run_id
	debugger_session_id = selected_debugger_session_id
	instance_nonce = selected_instance_nonce


func clear_run() -> void:
	run_id = 0
	debugger_session_id = 0


func requested_run(arguments: Dictionary) -> Dictionary:
	var requested = arguments.get("run_id")
	if (
		(not requested is int and not requested is float)
		or float(requested) != floorf(float(requested))
		or int(requested) < 1
	):
		return failure("run_id must be a positive integer")
	if int(requested) != run_id:
		return ErrorEnvelope.failure(
			"Runtime request belongs to another run", ErrorEnvelope.STALE_RUNTIME_ID,
			{"active_run_id": run_id}, false,
		)
	return success({"run_id": run_id})


func validate_path(path_value: Variant) -> Dictionary:
	if not path_value is String or path_value.is_empty() or path_value.length() > 512:
		return failure("Runtime node path must be a string up to 512 characters")
	if (
		path_value.begins_with("/")
		or path_value == ".."
		or path_value.begins_with("../")
		or "/../" in path_value
	):
		return failure("Runtime node path must remain inside the running scene")
	return success(path_value)


func resolve_node(path_value: Variant) -> Dictionary:
	var root := ignored_node.get_tree().current_scene
	if root == null:
		return ErrorEnvelope.failure(
			"No runtime scene is active", ErrorEnvelope.NO_ACTIVE_RUN,
		)
	var path_result := validate_path(path_value)
	if not path_result.ok:
		return path_result
	var node: Node = root if path_value == "." else root.get_node_or_null(NodePath(path_value))
	if node == null or (node != root and not root.is_ancestor_of(node)) or node == ignored_node:
		return ErrorEnvelope.failure(
			"Runtime node was not found", ErrorEnvelope.NOT_FOUND,
		)
	return success(node)


func condition_node(path_value: String) -> Node:
	var root := ignored_node.get_tree().current_scene
	if root == null:
		return null
	var node: Node = root if path_value == "." else root.get_node_or_null(NodePath(path_value))
	if node == null or node == ignored_node or (node != root and not root.is_ancestor_of(node)):
		return null
	return node


func runtime_id(node: Node) -> String:
	return JSON.stringify([
		instance_nonce, run_id, debugger_session_id, node.get_instance_id(),
	]).sha256_text()


func node_path(root: Node, node: Node) -> String:
	return "." if node == root else str(root.get_path_to(node)).left(512)


func expected_snapshot(arguments: Dictionary) -> Dictionary:
	var snapshot = arguments.get("_expected_snapshot", "")
	if not snapshot is String:
		return failure("Expected runtime snapshot is invalid")
	if not snapshot.is_empty() and (
		snapshot.length() != 64 or not snapshot.is_valid_hex_number()
	):
		return failure("Expected runtime snapshot is invalid")
	return success(snapshot)


func bounded_integer(
	value: Variant, label: String, minimum: int, maximum: int,
) -> Dictionary:
	if (not value is int and not value is float) or float(value) != floorf(float(value)):
		return failure("%s must be an integer" % label)
	var integer := int(value)
	if integer < minimum or integer > maximum:
		return failure("%s must be between %d and %d" % [label, minimum, maximum])
	return success(integer)


func success(result: Variant) -> Dictionary:
	return ErrorEnvelope.success(result)


func failure(message: String) -> Dictionary:
	return ErrorEnvelope.failure(message)
