@tool
extends EditorPlugin

const HOST := "127.0.0.1"
const BRIDGE_VERSION := "0.3.0"
const DEFAULT_PORT := 6505
const MAX_REQUEST_BYTES := 64 * 1024
const MAX_TREE_NODES := 200
const MAX_PROPERTIES := 64
const MAX_ASSETS := 100
const MAX_ASSET_SCAN := 5000
const TOKEN_PATH := "res://.godot/godot_mcp_token"
const CREATABLE_RESOURCE_TYPES := [
	"StandardMaterial3D", "ORMMaterial3D", "ShaderMaterial", "Environment",
	"Gradient", "Curve", "StyleBoxFlat", "AudioStreamRandomizer",
]

var _server := TCPServer.new()
var _clients: Array[Dictionary] = []
var _token := ""
var _port := DEFAULT_PORT
var _filesystem_generation := 0
var _run_id := 0
var _was_playing := false
var _last_run_exit_status := "never_started"
var _last_stop_reason := ""


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
	var filesystem := get_editor_interface().get_resource_filesystem()
	if not filesystem.filesystem_changed.is_connected(_on_filesystem_changed):
		filesystem.filesystem_changed.connect(_on_filesystem_changed)
	print("Godot MCP bridge listening on %s:%d" % [HOST, _port])


func _exit_tree() -> void:
	set_process(false)
	for client in _clients:
		(client.peer as StreamPeerTCP).disconnect_from_host()
	_clients.clear()
	_server.stop()


func _process(_delta: float) -> void:
	var playing := get_editor_interface().is_playing_scene()
	if playing and not _was_playing:
		_run_id += 1
		_last_run_exit_status = "running"
		_last_stop_reason = ""
	elif not playing and _was_playing:
		_last_run_exit_status = "stopped"
		if _last_stop_reason.is_empty():
			_last_stop_reason = "run_ended"
	_was_playing = playing

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
		"capabilities":
			return _success(_capabilities())
		"state":
			return _success(_editor_state())
		"assets":
			return _list_assets(arguments)
		"asset_info":
			return _asset_info(arguments)
		"scan_asset":
			return _scan_asset(arguments)
		"create_resource":
			return _create_resource(arguments)
		"create_scene":
			return _create_scene(arguments)
		"open_scene":
			return _open_scene(arguments)
		"tree":
			return _scene_tree()
		"inspect":
			return _inspect_node(arguments)
		"add_node":
			return _add_node(arguments)
		"instantiate_scene":
			return _instantiate_scene(arguments)
		"set_property":
			return _set_property(arguments)
		"select":
			return _select_node(arguments)
		"control":
			return _scene_control(arguments)
		_:
			return _failure("Unknown command")


func _capabilities() -> Dictionary:
	return {
		"bridge_version": BRIDGE_VERSION,
		"godot_version": str(Engine.get_version_info().get("string", "Godot 4")),
		"commands": [
			"capabilities", "state", "assets", "asset_info", "scan_asset",
			"create_resource", "create_scene", "open_scene", "tree", "inspect",
			"add_node", "instantiate_scene", "set_property", "select", "control",
		],
		"features": {
			"runtime_inspection": false,
			"game_view_capture": false,
			"input_injection": false,
			"diagnostics": false,
		},
		"limits": {
			"request_bytes": MAX_REQUEST_BYTES,
			"tree_nodes": MAX_TREE_NODES,
			"properties": MAX_PROPERTIES,
			"assets": MAX_ASSETS,
			"asset_scan": MAX_ASSET_SCAN,
		},
	}


func _editor_state() -> Dictionary:
	var root := get_editor_interface().get_edited_scene_root()
	var filesystem := get_editor_interface().get_resource_filesystem()
	var playing := get_editor_interface().is_playing_scene()
	var selected: Array[String] = []
	if root != null:
		for node in get_editor_interface().get_selection().get_selected_nodes():
			if node == root:
				selected.append(".")
			elif root.is_ancestor_of(node):
				selected.append(str(root.get_path_to(node)))
	return {
		"godot": str(Engine.get_version_info().get("string", "Godot 4")),
		"project_name": str(ProjectSettings.get_setting("application/config/name", "")),
		"project_path": ProjectSettings.globalize_path("res://"),
		"main_scene": str(ProjectSettings.get_setting("application/run/main_scene", "")),
		"bridge_version": BRIDGE_VERSION,
		"bridge_port": _port,
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
	}


func _on_filesystem_changed() -> void:
	_filesystem_generation += 1


func _list_assets(arguments: Dictionary) -> Dictionary:
	var folder_value = arguments.get("folder", ".")
	var folder_path := "res://"
	if folder_value != ".":
		var checked := _project_path(folder_value)
		if not checked.ok:
			return checked
		folder_path = checked.result
	var asset_type = arguments.get("type", "all")
	var allowed_types := ["all", "scene", "script", "image", "model", "audio", "font", "material", "resource"]
	if not asset_type is String or asset_type not in allowed_types:
		return _failure("Type filter is invalid")
	var limit_value = arguments.get("limit", 50)
	if not limit_value is int and not limit_value is float:
		return _failure("Limit must be a number")
	var limit := int(limit_value)
	if limit < 1 or limit > MAX_ASSETS:
		return _failure("Limit must be between 1 and 100")

	var filesystem := get_editor_interface().get_resource_filesystem()
	var directory := filesystem.get_filesystem_path(folder_path)
	if directory == null:
		return _failure("Asset folder not found")
	var output: Array[Dictionary] = []
	var state := {"visited": 0, "truncated": false}
	_collect_assets(directory, asset_type, limit, output, state)
	return _success({"assets": output, "truncated": state.truncated})


func _collect_assets(
	directory: EditorFileSystemDirectory,
	asset_type: String,
	limit: int,
	output: Array[Dictionary],
	state: Dictionary,
) -> void:
	var raw_directory := DirAccess.open(directory.get_path())
	for index in directory.get_file_count():
		state.visited += 1
		if state.visited > MAX_ASSET_SCAN:
			state.truncated = true
			return
		var file_name := directory.get_file(index)
		if raw_directory != null and raw_directory.is_link(file_name):
			continue
		var path := directory.get_path().path_join(file_name)
		var resource_type := directory.get_file_type(index)
		var category := _asset_category(path, resource_type)
		if asset_type == "all" or category == asset_type or (asset_type == "resource" and category == "resource"):
			if output.size() >= limit:
				state.truncated = true
				return
			output.append({"path": path, "type": resource_type, "category": category})
	for index in directory.get_subdir_count():
		if state.truncated:
			return
		var subdirectory := directory.get_subdir(index)
		if raw_directory != null and raw_directory.is_link(subdirectory.get_path().get_file()):
			continue
		_collect_assets(subdirectory, asset_type, limit, output, state)


func _asset_info(arguments: Dictionary) -> Dictionary:
	var checked := _project_path(arguments.get("path"))
	if not checked.ok:
		return checked
	var path := checked.result as String
	if not FileAccess.file_exists(path):
		return _failure("Asset not found")
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return _failure("Cannot read asset information")
	var size := file.get_length()
	var resource_type: String = get_editor_interface().get_resource_filesystem().get_file_type(path)
	if resource_type.is_empty() and path.get_extension().to_lower() in ["tres", "res", "tscn", "scn"]:
		var loaded := ResourceLoader.load(path)
		if loaded != null:
			resource_type = loaded.get_class()
	var dependencies: Array[String] = []
	for dependency in ResourceLoader.get_dependencies(path):
		var parts := str(dependency).split("::")
		dependencies.append(parts[parts.size() - 1])
		if dependencies.size() >= 50:
			break
	return _success({
		"path": path,
		"type": resource_type,
		"category": _asset_category(path, resource_type),
		"bytes": size,
		"imported": FileAccess.file_exists(path + ".import"),
		"loadable": ResourceLoader.exists(path),
		"dependencies": dependencies,
		"dependencies_truncated": dependencies.size() >= 50,
	})


func _scan_asset(arguments: Dictionary) -> Dictionary:
	var checked := _project_path(arguments.get("path"))
	if not checked.ok:
		return checked
	var filesystem := get_editor_interface().get_resource_filesystem()
	if filesystem.is_scanning():
		return _success({"path": checked.result, "scan": "already_running"})
	filesystem.scan()
	return _success({"path": checked.result, "scan": "queued"})


func _create_resource(arguments: Dictionary) -> Dictionary:
	var checked := _project_path(arguments.get("path"), true, PackedStringArray(["tres"]))
	if not checked.ok:
		return checked
	var path := checked.result as String
	if FileAccess.file_exists(path):
		return _failure("Resource already exists")
	if not DirAccess.dir_exists_absolute(ProjectSettings.globalize_path(path.get_base_dir())):
		return _failure("Resource folder does not exist")
	var resource_type = arguments.get("type")
	if not resource_type is String or resource_type not in CREATABLE_RESOURCE_TYPES:
		return _failure("Resource type is not allowed")
	var properties = arguments.get("properties", {})
	if not properties is Dictionary or properties.size() > 32:
		return _failure("Properties must be an object with at most 32 entries")
	var object = ClassDB.instantiate(resource_type)
	if not object is Resource:
		return _failure("Could not instantiate resource")
	var resource := object as Resource
	for property_name in properties:
		if not property_name is String or property_name.is_empty() or property_name.length() > 128:
			return _failure("Resource property name is invalid")
		var property_info: Dictionary = {}
		for info in resource.get_property_list():
			if str(info.name) == property_name and (int(info.usage) & PROPERTY_USAGE_EDITOR) != 0:
				property_info = info
				break
		if property_info.is_empty():
			return _failure("Editable resource property not found: %s" % property_name)
		var converted := _convert_value(properties[property_name], int(property_info.type))
		if not converted.ok:
			return _failure("Invalid %s: %s" % [property_name, converted.error])
		resource.set(property_name, converted.result)
	var save_error := ResourceSaver.save(resource, path)
	if save_error != OK:
		return _failure("Could not save resource")
	get_editor_interface().get_resource_filesystem().update_file(path)
	return _success({"path": path, "type": resource_type, "properties": properties.keys()})


func _create_scene(arguments: Dictionary) -> Dictionary:
	var checked := _project_path(arguments.get("path"), true, PackedStringArray(["tscn"]))
	if not checked.ok:
		return checked
	var path := checked.result as String
	if FileAccess.file_exists(path):
		return _failure("Scene already exists")
	var parent_folder := path.get_base_dir()
	if not DirAccess.dir_exists_absolute(ProjectSettings.globalize_path(parent_folder)):
		return _failure("Scene folder does not exist")
	var root_type = arguments.get("root_type")
	if not root_type is String or root_type.is_empty() or root_type.length() > 128:
		return _failure("Root type must be a non-empty string up to 128 characters")
	if not ClassDB.class_exists(root_type) or not ClassDB.can_instantiate(root_type) or not ClassDB.is_parent_class(root_type, "Node"):
		return _failure("Root type must be an instantiable built-in Node class")
	var valid_name := _checked_node_name(arguments.get("root_name"))
	if not valid_name.ok:
		return valid_name
	var root_object = ClassDB.instantiate(root_type)
	if not root_object is Node:
		if root_object != null:
			root_object.free()
		return _failure("Could not instantiate root node")
	var root := root_object as Node
	root.name = valid_name.result
	var packed := PackedScene.new()
	var pack_error := packed.pack(root)
	if pack_error != OK:
		root.free()
		return _failure("Could not pack scene")
	var save_error := ResourceSaver.save(packed, path)
	root.free()
	if save_error != OK:
		return _failure("Could not save scene")
	get_editor_interface().get_resource_filesystem().update_file(path)
	return _success({"path": path, "root_type": root_type, "root_name": valid_name.result})


func _open_scene(arguments: Dictionary) -> Dictionary:
	var checked := _project_path(arguments.get("path"), false, PackedStringArray(["tscn", "scn"]))
	if not checked.ok:
		return checked
	var path := checked.result as String
	if not FileAccess.file_exists(path):
		return _failure("PackedScene not found")
	var resource := ResourceLoader.load(path)
	if not resource is PackedScene:
		return _failure("PackedScene not found")
	get_editor_interface().get_resource_filesystem().update_file(path)
	get_editor_interface().open_scene_from_path(path)
	return _success({"path": path, "open": "requested"})


func _asset_category(path: String, resource_type: String) -> String:
	var extension := path.get_extension().to_lower()
	if extension in ["tscn", "scn"]:
		return "scene"
	if extension in ["gd", "cs"]:
		return "script"
	if extension in ["bmp", "exr", "hdr", "jpeg", "jpg", "png", "svg", "webp"]:
		return "image"
	if extension in ["fbx", "glb", "gltf", "obj"]:
		return "model"
	if extension in ["mp3", "ogg", "wav"]:
		return "audio"
	if extension in ["otf", "ttf", "woff", "woff2"]:
		return "font"
	if "Material" in resource_type or extension in ["material", "shader"]:
		return "material"
	return "resource"


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


func _add_node(arguments: Dictionary) -> Dictionary:
	var found := _find_node(arguments.get("parent"))
	if not found.ok:
		return found
	var node_type = arguments.get("type")
	if not node_type is String or node_type.is_empty() or node_type.length() > 128:
		return _failure("Node type must be a non-empty string up to 128 characters")
	if not ClassDB.class_exists(node_type) or not ClassDB.can_instantiate(node_type) or not ClassDB.is_parent_class(node_type, "Node"):
		return _failure("Type must be an instantiable built-in Node class")
	var valid_name := _checked_node_name(arguments.get("name"))
	if not valid_name.ok:
		return valid_name
	var parent := found.result as Node
	if _has_child_named(parent, valid_name.result):
		return _failure("Parent already has a child with that name")
	var object = ClassDB.instantiate(node_type)
	if not object is Node:
		if object != null:
			object.free()
		return _failure("Could not instantiate node")
	var node := object as Node
	node.name = valid_name.result
	return _commit_added_node(parent, node, "MCP: add %s" % valid_name.result)


func _instantiate_scene(arguments: Dictionary) -> Dictionary:
	var found := _find_node(arguments.get("parent"))
	if not found.ok:
		return found
	var checked := _project_path(arguments.get("scene"), false, PackedStringArray(["tscn", "scn"]))
	if not checked.ok:
		return checked
	var scene_path := checked.result as String
	var edited_root := get_editor_interface().get_edited_scene_root()
	if edited_root.scene_file_path == scene_path:
		return _failure("Cannot instantiate the edited scene inside itself")
	var valid_name := _checked_node_name(arguments.get("name"))
	if not valid_name.ok:
		return valid_name
	var parent := found.result as Node
	if _has_child_named(parent, valid_name.result):
		return _failure("Parent already has a child with that name")
	var resource := ResourceLoader.load(scene_path)
	if not resource is PackedScene:
		return _failure("PackedScene not found")
	var node := (resource as PackedScene).instantiate(PackedScene.GEN_EDIT_STATE_INSTANCE)
	if node == null:
		return _failure("Could not instantiate scene")
	node.name = valid_name.result
	return _commit_added_node(parent, node, "MCP: instantiate %s" % valid_name.result)


func _commit_added_node(parent: Node, node: Node, action: String) -> Dictionary:
	var root := get_editor_interface().get_edited_scene_root()
	var undo := get_undo_redo()
	undo.create_action(action)
	undo.add_do_method(parent, "add_child", node)
	undo.add_do_method(node, "set_owner", root)
	undo.add_do_reference(node)
	undo.add_undo_method(parent, "remove_child", node)
	undo.commit_action()
	return _success({
		"path": str(root.get_path_to(node)),
		"type": node.get_class(),
		"name": node.name,
	})


func _has_child_named(parent: Node, node_name: String) -> bool:
	for child in parent.get_children():
		if child.name == node_name:
			return true
	return false


func _checked_node_name(value: Variant) -> Dictionary:
	if not value is String or value.is_empty() or value.length() > 128:
		return _failure("Node name must be a non-empty string up to 128 characters")
	if value.validate_node_name() != value:
		return _failure("Node name contains invalid characters")
	return _success(value)


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
			if not _was_playing:
				_run_id += 1
			_was_playing = true
			_last_run_exit_status = "running"
			_last_stop_reason = ""
			return _success({"message": "Scene started", "run_id": _run_id})
		"stop":
			_last_stop_reason = "requested"
			get_editor_interface().stop_playing_scene()
			return _success({"message": "Scene stopped", "run_id": _run_id if _run_id > 0 else null})
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
