extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")

var _gateway: EditorDebuggerPlugin
var _cursors: RefCounted


func _init(gateway: EditorDebuggerPlugin, cursors: RefCounted) -> void:
	_gateway = gateway
	_cursors = cursors


func scene_tree(arguments: Dictionary) -> Dictionary:
	var query := [
		arguments.get("root", "."),
		arguments.get("max_depth", 3),
		arguments.get("class", ""),
		arguments.get("limit", 50),
	]
	var prepared := _prepare(arguments, "runtime_scene_tree", query)
	if not prepared.ok:
		return prepared
	var request_arguments := arguments.duplicate()
	request_arguments.erase("tree_scope")
	request_arguments.erase("cursor")
	request_arguments["_offset"] = prepared.result.offset
	request_arguments["_expected_snapshot"] = prepared.result.snapshot
	return _gateway.begin_request(
		"tree", request_arguments,
		Callable(self, "_complete_page").bind(
			"runtime_scene_tree", query, int(prepared.result.offset), "nodes",
		),
	)


func inspect_node(arguments: Dictionary) -> Dictionary:
	var query := [
		arguments.get("path"),
		arguments.get("runtime_id", ""),
		arguments.get("property", ""),
		arguments.get("category", ""),
		arguments.get("limit", 24),
	]
	var prepared := _prepare(arguments, "runtime_node_properties", query)
	if not prepared.ok:
		return prepared
	var request_arguments := arguments.duplicate()
	request_arguments.erase("tree_scope")
	request_arguments.erase("cursor")
	request_arguments["_offset"] = prepared.result.offset
	request_arguments["_expected_snapshot"] = prepared.result.snapshot
	return _gateway.begin_request(
		"inspect", request_arguments,
		Callable(self, "_complete_page").bind(
			"runtime_node_properties", query, int(prepared.result.offset), "properties",
		),
	)


func _prepare(arguments: Dictionary, kind: String, query: Variant) -> Dictionary:
	if not arguments.has("cursor"):
		return ErrorEnvelope.success({"offset": 0, "snapshot": ""})
	return _cursors.prepare(arguments.cursor, kind, query)


func _complete_page(
	response: Dictionary, kind: String, query: Variant, offset: int, items_field: String,
) -> Dictionary:
	if not response.get("ok", false):
		return response
	var result = response.get("result")
	if not result is Dictionary:
		return _invalid_response()
	var snapshot = result.get("snapshot_id")
	var items = result.get(items_field)
	var continuation = result.get("continuation_available")
	if (
		not snapshot is String
		or snapshot.length() != 64
		or not snapshot.is_valid_hex_number()
		or not items is Array
		or not continuation is bool
		or result.get("scope") != "runtime"
	):
		return _invalid_response()
	var cursor: Variant = null
	if continuation:
		cursor = _cursors.issue(kind, query, snapshot, offset + items.size())
	result["cursor"] = cursor
	return ErrorEnvelope.success(result)


func _invalid_response() -> Dictionary:
	return ErrorEnvelope.failure(
		"Runtime probe returned invalid inspection data", ErrorEnvelope.INVALID_ARGUMENT,
	)
