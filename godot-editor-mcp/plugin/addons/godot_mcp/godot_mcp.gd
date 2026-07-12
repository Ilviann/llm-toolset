@tool
extends EditorPlugin

const HOST := "127.0.0.1"
const DEFAULT_PORT := 6505
const MAX_REQUEST_BYTES := 64 * 1024
const MAX_TREE_NODES := 200
const MAX_PROPERTIES := 64
const TOKEN_PATH := "res://.godot/godot_mcp_token"

var _server := TCPServer.new()
var _clients: Array[Dictionary] = []
var _token := ""
var _port := DEFAULT_PORT


func _enter_tree() -> void:
	_token = _load_or_create_token()
	_port = int(ProjectSettings.get_setting("godot_mcp/port", DEFAULT_PORT))
	if _port < 1 or _port > 65535:
		push_error("Godot MCP bridge port must be between 1 and 65535")
		return
	var error := _server.listen(_port, HOST)
	if error != OK:
		push_error("Godot MCP bridge could not listen on %s:%d (error %d)" % [HOST, _port, error])
		return
	set_process(true)
	print("Godot MCP bridge listening on %s:%d" % [HOST, _port])


func _exit_tree() -> void:
	set_process(false)
	for client in _clients:
		(client.peer as StreamPeerTCP).disconnect_from_host()
	_clients.clear()
	_server.stop()


func _process(_delta: float) -> void:
	while _server.is_connection_available():
		var peer := _server.take_connection()
		_clients.append({"peer": peer, "buffer": PackedByteArray()})

	for index in range(_clients.size() - 1, -1, -1):
		var client := _clients[index]
		var peer := client.peer as StreamPeerTCP
		peer.poll()
		if peer.get_status() != StreamPeerTCP.STATUS_CONNECTED:
			_clients.remove_at(index)
			continue
		var available := peer.get_available_bytes()
		if available <= 0:
			continue
		var received := peer.get_data(available)
		if received[0] != OK:
			peer.disconnect_from_host()
			_clients.remove_at(index)
			continue
		var buffer := client.buffer as PackedByteArray
		buffer.append_array(received[1])
		if buffer.size() > MAX_REQUEST_BYTES:
			_send(peer, _failure("Request is too large"))
			_clients.remove_at(index)
			continue
		var newline := buffer.find(10)
		if newline < 0:
			client.buffer = buffer
			continue
		var line := buffer.slice(0, newline).get_string_from_utf8()
		_send(peer, _handle_line(line))
		_clients.remove_at(index)


func _send(peer: StreamPeerTCP, response: Dictionary) -> void:
	peer.put_data((JSON.stringify(response) + "\n").to_utf8_buffer())
	peer.disconnect_from_host()


func _handle_line(line: String) -> Dictionary:
	var request = JSON.parse_string(line)
	if not request is Dictionary:
		return _failure("Invalid request")
	if not _constant_time_equal(str(request.get("token", "")), _token):
		return _failure("Unauthorized")
	var command = request.get("command")
	var arguments = request.get("arguments", {})
	if not command is String or not arguments is Dictionary:
		return _failure("Invalid request")

	match command:
		"state":
			return _success(_editor_state())
		"tree":
			return _scene_tree()
		"inspect":
			return _inspect_node(arguments)
		"set_property":
			return _set_property(arguments)
		"select":
			return _select_node(arguments)
		"control":
			return _scene_control(arguments)
		_:
			return _failure("Unknown command")


func _editor_state() -> Dictionary:
	var root := get_editor_interface().get_edited_scene_root()
	var selected: Array[String] = []
	if root != null:
		for node in get_editor_interface().get_selection().get_selected_nodes():
			if node == root:
				selected.append(".")
			elif root.is_ancestor_of(node):
				selected.append(str(root.get_path_to(node)))
	return {
		"godot": str(Engine.get_version_info().get("string", "Godot 4")),
		"scene": "" if root == null else root.scene_file_path,
		"root": "" if root == null else root.name,
		"selected": selected,
		"playing": get_editor_interface().is_playing_scene(),
	}


func _scene_tree() -> Dictionary:
	var root := get_editor_interface().get_edited_scene_root()
	if root == null:
		return _failure("No scene is open")
	var nodes: Array[Dictionary] = []
	_collect_nodes(root, root, nodes, 0)
	return _success({"nodes": nodes, "truncated": nodes.size() >= MAX_TREE_NODES})


func _collect_nodes(root: Node, node: Node, output: Array[Dictionary], depth: int) -> void:
	if output.size() >= MAX_TREE_NODES:
		return
	output.append({
		"path": "." if node == root else str(root.get_path_to(node)),
		"type": node.get_class(),
		"depth": depth,
	})
	for child in node.get_children():
		if output.size() >= MAX_TREE_NODES:
			return
		_collect_nodes(root, child, output, depth + 1)


func _inspect_node(arguments: Dictionary) -> Dictionary:
	var found := _find_node(arguments.get("path"))
	if not found.ok:
		return found
	var node := found.result as Node
	var properties: Array[Dictionary] = []
	for info in node.get_property_list():
		if properties.size() >= MAX_PROPERTIES:
			break
		var usage := int(info.get("usage", 0))
		if (usage & PROPERTY_USAGE_EDITOR) == 0:
			continue
		var property_name := str(info.name)
		properties.append({
			"name": property_name,
			"type": type_string(int(info.type)),
			"value": _encode_value(node.get(property_name)),
		})
	return _success({
		"path": arguments.path,
		"type": node.get_class(),
		"properties": properties,
		"truncated": properties.size() >= MAX_PROPERTIES,
	})


func _set_property(arguments: Dictionary) -> Dictionary:
	var found := _find_node(arguments.get("path"))
	if not found.ok:
		return found
	var property_name = arguments.get("property")
	if not property_name is String or property_name.is_empty() or property_name.length() > 128:
		return _failure("Property must be a non-empty string up to 128 characters")
	if not arguments.has("value"):
		return _failure("Missing value")
	var node := found.result as Node
	var property_info: Dictionary = {}
	for info in node.get_property_list():
		if str(info.name) == property_name and (int(info.usage) & PROPERTY_USAGE_EDITOR) != 0:
			property_info = info
			break
	if property_info.is_empty():
		return _failure("Editable property not found")
	var converted := _convert_value(arguments.value, int(property_info.type))
	if not converted.ok:
		return converted
	var previous = node.get(property_name)
	var undo := get_undo_redo()
	undo.create_action("MCP: set %s" % property_name)
	undo.add_do_property(node, property_name, converted.result)
	undo.add_undo_property(node, property_name, previous)
	undo.commit_action()
	return _success({"path": arguments.path, "property": property_name, "value": _encode_value(node.get(property_name))})


func _select_node(arguments: Dictionary) -> Dictionary:
	var found := _find_node(arguments.get("path"))
	if not found.ok:
		return found
	var selection := get_editor_interface().get_selection()
	selection.clear()
	selection.add_node(found.result)
	return _success("Selected %s" % arguments.path)


func _scene_control(arguments: Dictionary) -> Dictionary:
	var action = arguments.get("action")
	match action:
		"save":
			if get_editor_interface().get_edited_scene_root() == null:
				return _failure("No scene is open")
			get_editor_interface().save_scene()
			return _success("Scene saved")
		"run":
			if get_editor_interface().get_edited_scene_root() == null:
				return _failure("No scene is open")
			get_editor_interface().play_current_scene()
			return _success("Scene started")
		"stop":
			get_editor_interface().stop_playing_scene()
			return _success("Scene stopped")
		_:
			return _failure("Action must be save, run, or stop")


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


func _load_or_create_token() -> String:
	if FileAccess.file_exists(TOKEN_PATH):
		var existing := FileAccess.get_file_as_string(TOKEN_PATH).strip_edges().to_lower()
		if existing.length() == 64 and existing.is_valid_hex_number():
			return existing
	var token := Crypto.new().generate_random_bytes(32).hex_encode()
	var file := FileAccess.open(TOKEN_PATH, FileAccess.WRITE)
	if file == null:
		push_error("Godot MCP bridge could not create its token")
		return token
	file.store_string(token + "\n")
	return token


func _constant_time_equal(left: String, right: String) -> bool:
	var different := left.length() ^ right.length()
	for index in range(max(left.length(), right.length())):
		var a := left.unicode_at(index) if index < left.length() else 0
		var b := right.unicode_at(index) if index < right.length() else 0
		different |= a ^ b
	return different == 0


func _success(result: Variant) -> Dictionary:
	return {"ok": true, "result": result}


func _failure(message: String) -> Dictionary:
	return {"ok": false, "error": message}
