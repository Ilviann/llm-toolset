@tool
extends EditorPlugin

const HOST := "127.0.0.1"
const BRIDGE_VERSION := "0.4.0"
const DEFAULT_PORT := 6505
const MAX_REQUEST_BYTES := 64 * 1024
const MAX_TREE_NODES := 200
const MAX_PROPERTIES := 64
const MAX_ASSETS := 100
const MAX_ASSET_SCAN := 5000
const MAX_SETTINGS := 100
const MAX_SETTING_CHANGES := 32
const MAX_INPUT_EVENTS := 32
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
		"project_settings_get":
			return _project_settings_get(arguments)
		"project_settings_patch":
			return _project_settings_patch(arguments)
		"input_map_patch":
			return _input_map_patch(arguments)
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
			"project_settings_get", "project_settings_patch", "input_map_patch",
		],
		"features": {
			"runtime_inspection": false,
			"game_view_capture": false,
			"input_injection": false,
			"diagnostics": false,
			"project_settings": true,
			"input_map_editing": true,
		},
		"project_settings": {
			"value_types": [
				"null", "bool", "int", "float", "string", "string_name",
				"node_path", "vector2", "vector2i", "vector3", "vector3i",
				"color", "array", "dictionary", "packed_string_array",
			],
			"input_event_types": [
				"key", "mouse_button", "joypad_button", "joypad_motion",
			],
		},
		"limits": {
			"request_bytes": MAX_REQUEST_BYTES,
			"tree_nodes": MAX_TREE_NODES,
			"properties": MAX_PROPERTIES,
			"assets": MAX_ASSETS,
			"asset_scan": MAX_ASSET_SCAN,
			"settings": MAX_SETTINGS,
			"setting_changes": MAX_SETTING_CHANGES,
			"input_events": MAX_INPUT_EVENTS,
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


func _project_settings_get(arguments: Dictionary) -> Dictionary:
	if not _only_keys(arguments, ["key", "recursive"]):
		return _failure("project_settings_get contains an unsupported field")
	var checked := _checked_setting_key(arguments.get("key"), false)
	if not checked.ok:
		return checked
	var key := checked.result as String
	var recursive = arguments.get("recursive", false)
	if not recursive is bool:
		return _failure("recursive must be a boolean")
	if not recursive:
		return _success(_setting_record(key))
	var settings: Array[Dictionary] = []
	var seen := {}
	for info in ProjectSettings.get_property_list():
		var candidate := str(info.get("name", ""))
		if candidate in seen or not (candidate == key or candidate.begins_with(key + "/")):
			continue
		if not _checked_setting_key(candidate, false).ok:
			continue
		seen[candidate] = true
		settings.append(_setting_record(candidate))
		if settings.size() >= MAX_SETTINGS:
			break
	settings.sort_custom(func(left: Dictionary, right: Dictionary) -> bool: return left.key < right.key)
	return _success({
		"prefix": key,
		"settings": settings,
		"truncated": settings.size() >= MAX_SETTINGS,
	})


func _setting_record(key: String) -> Dictionary:
	var exists := ProjectSettings.has_setting(key)
	var value = ProjectSettings.get_setting(key) if exists else null
	var has_default := exists and ProjectSettings.property_can_revert(key)
	var default_value = ProjectSettings.property_get_revert(key) if has_default else null
	return {
		"key": key,
		"exists": exists,
		"value": _encode_setting_value(value),
		"type": type_string(typeof(value)) if exists else "nil",
		"has_default": has_default,
		"default": _encode_setting_value(default_value) if has_default else null,
		"differs_from_default": exists and has_default and value != default_value,
		"reload": _setting_reload_requirement(key),
	}


func _project_settings_patch(arguments: Dictionary) -> Dictionary:
	if not _only_keys(arguments, ["changes", "save", "dry_run"]):
		return _failure("project_settings_patch contains an unsupported field")
	var changes = arguments.get("changes")
	if not changes is Array or changes.is_empty() or changes.size() > MAX_SETTING_CHANGES:
		return _failure("changes must contain between 1 and 32 entries")
	var save = arguments.get("save", true)
	var dry_run = arguments.get("dry_run", false)
	if not save is bool or not dry_run is bool:
		return _failure("save and dry_run must be booleans")
	var prepared: Array[Dictionary] = []
	var keys := {}
	for change in changes:
		if not change is Dictionary:
			return _failure("Each change must be an object")
		if not _only_keys(change, ["key", "expected", "value"]):
			return _failure("A change contains an unsupported field")
		if not change.has("value"):
			return _failure("Each change must include value")
		var checked := _checked_setting_key(change.get("key"), true)
		if not checked.ok:
			return checked
		var key := checked.result as String
		if key in keys:
			return _failure("Duplicate setting key: %s" % key)
		keys[key] = true
		var existed := ProjectSettings.has_setting(key)
		var before = ProjectSettings.get_setting(key) if existed else null
		if change.has("expected"):
			if not existed:
				if change.expected != null:
					return _failure("Compare-and-swap failed for %s" % key)
			else:
				var expected := _decode_setting_value(change.expected, typeof(before))
				if not expected.ok:
					return _failure("Invalid expected value for %s: %s" % [key, expected.error])
				if expected.result != before:
					return _failure("Compare-and-swap failed for %s" % key)
		var converted := _decode_setting_value(change.value, typeof(before) if existed else TYPE_NIL)
		if not converted.ok:
			return _failure("Invalid value for %s: %s" % [key, converted.error])
		prepared.append({
			"key": key, "existed": existed, "before_raw": before,
			"after_raw": converted.result,
			"diff": {
				"key": key,
				"before": _encode_setting_value(before) if existed else null,
				"after": _encode_setting_value(converted.result),
				"changed": not existed or before != converted.result,
				"reload": _setting_reload_requirement(key),
			},
		})
	var diffs: Array[Dictionary] = []
	for item in prepared:
		diffs.append(item.diff)
	if not dry_run:
		for item in prepared:
			ProjectSettings.set_setting(item.key, item.after_raw)
			if save:
				var error := ProjectSettings.save()
				if error != OK:
					_restore_settings(prepared)
					ProjectSettings.save()
					return _failure("Could not save project settings (Godot error %d); transaction rolled back" % error)
	return _success({
		"diff": diffs,
		"dry_run": dry_run,
		"saved": save and not dry_run,
		"requirements": _combined_reload_requirements(prepared),
	})


func _restore_settings(prepared: Array[Dictionary]) -> void:
	for item in prepared:
		if item.existed:
			ProjectSettings.set_setting(item.key, item.before_raw)
		else:
			ProjectSettings.clear(item.key)


func _combined_reload_requirements(prepared: Array[Dictionary]) -> Dictionary:
	var reload := false
	var restart := false
	for item in prepared:
		if not item.diff.changed:
			continue
		var requirement := item.diff.reload as String
		reload = reload or requirement == "project_reload"
		restart = restart or requirement == "editor_restart"
	return {
		"editor_refresh": false,
		"project_reload": reload,
		"editor_restart": restart,
	}


func _setting_reload_requirement(key: String) -> String:
	if key.begins_with("godot_mcp/") or key.begins_with("rendering/renderer/") or key.begins_with("audio/driver/"):
		return "editor_restart"
	if key.begins_with("input/"):
		return "none"
	return "project_reload"


func _checked_setting_key(value: Variant, writable: bool) -> Dictionary:
	if not value is String or value.is_empty() or value.length() > 256:
		return _failure("Setting key must be a non-empty string up to 256 characters")
	if value.begins_with("/") or value.ends_with("/") or "//" in value or "\\" in value:
		return _failure("Setting key is invalid")
	for character in value:
		if character.unicode_at(0) < 32:
			return _failure("Setting key contains a control character")
	var lowered: String = value.to_lower()
	var secret_terms := ["password", "secret", "token", "credential", "api_key", "private_key"]
	for term in secret_terms:
		if term in lowered:
			return _failure("Secret-bearing project settings are not exposed")
	if writable and (lowered.begins_with("editor/") or lowered.begins_with("_")):
		return _failure("Editor-only or internal project settings cannot be changed")
	if writable and lowered.begins_with("input/"):
		return _failure("Use input_map_patch for Input Map settings")
	return _success(value)


func _decode_setting_value(value: Variant, target_type: int, depth := 0) -> Dictionary:
	if depth > 6:
		return _failure("Value nesting is too deep")
	if value is String and value.length() > 4096:
		return _failure("String value is too long")
	if target_type == TYPE_NIL:
		if value == null or value is bool or value is int or value is float or value is String:
			return _success(value)
		if value is Array:
			var new_array: Array = []
			if value.size() > 100:
				return _failure("Array has more than 100 entries")
			for child in value:
				var decoded := _decode_setting_value(child, TYPE_NIL, depth + 1)
				if not decoded.ok:
					return decoded
				new_array.append(decoded.result)
			return _success(new_array)
		if value is Dictionary:
			var new_dictionary := {}
			if value.size() > 100:
				return _failure("Dictionary has more than 100 entries")
			for child_key in value:
				if not child_key is String or child_key.length() > 256:
					return _failure("Dictionary keys must be strings up to 256 characters")
				var decoded := _decode_setting_value(value[child_key], TYPE_NIL, depth + 1)
				if not decoded.ok:
					return decoded
				new_dictionary[child_key] = decoded.result
			return _success(new_dictionary)
		return _failure("Unsupported JSON value")
	if target_type == TYPE_INT and (value is int or value is float):
		return _success(int(value))
	if target_type == TYPE_FLOAT and (value is int or value is float):
		return _success(float(value))
	if target_type == TYPE_STRING and value is String:
		return _success(value)
	if target_type == TYPE_STRING_NAME and value is String:
		return _success(StringName(value))
	if target_type == TYPE_NODE_PATH and value is String:
		return _success(NodePath(value))
	if target_type == TYPE_VECTOR2 and _number_array(value, 2):
		return _success(Vector2(float(value[0]), float(value[1])))
	if target_type == TYPE_VECTOR2I and _number_array(value, 2):
		return _success(Vector2i(int(value[0]), int(value[1])))
	if target_type == TYPE_VECTOR3 and _number_array(value, 3):
		return _success(Vector3(float(value[0]), float(value[1]), float(value[2])))
	if target_type == TYPE_VECTOR3I and _number_array(value, 3):
		return _success(Vector3i(int(value[0]), int(value[1]), int(value[2])))
	if target_type == TYPE_COLOR and _number_array(value, 4):
		return _success(Color(float(value[0]), float(value[1]), float(value[2]), float(value[3])))
	if target_type == TYPE_PACKED_STRING_ARRAY and value is Array:
		var strings := PackedStringArray()
		for item in value:
			if not item is String:
				return _failure("Packed string array entries must be strings")
			strings.append(item)
		return _success(strings)
	if target_type in [TYPE_ARRAY, TYPE_DICTIONARY]:
		var decoded := _decode_setting_value(value, TYPE_NIL, depth)
		if decoded.ok and typeof(decoded.result) == target_type:
			return decoded
	if target_type == typeof(value):
		return _success(value)
	return _failure("Value does not match setting type %s" % type_string(target_type))


func _number_array(value: Variant, size: int) -> bool:
	if not value is Array or value.size() != size:
		return false
	for item in value:
		if not item is int and not item is float:
			return false
	return true


func _encode_setting_value(value: Variant, depth := 0) -> Variant:
	if depth > 6:
		return "..."
	if value is InputEvent:
		return _normalize_input_event(value)
	match typeof(value):
		TYPE_NIL, TYPE_BOOL, TYPE_INT, TYPE_FLOAT:
			return value
		TYPE_STRING, TYPE_STRING_NAME, TYPE_NODE_PATH:
			return str(value).left(4096)
		TYPE_VECTOR2, TYPE_VECTOR2I:
			return [value.x, value.y]
		TYPE_VECTOR3, TYPE_VECTOR3I:
			return [value.x, value.y, value.z]
		TYPE_COLOR:
			return [value.r, value.g, value.b, value.a]
		TYPE_ARRAY, TYPE_PACKED_STRING_ARRAY:
			var output: Array = []
			for item in value.slice(0, 100):
				output.append(_encode_setting_value(item, depth + 1))
			return output
		TYPE_DICTIONARY:
			var output := {}
			for key in value.keys().slice(0, 100):
				output[str(key)] = _encode_setting_value(value[key], depth + 1)
			return output
		_:
			return "<unsupported:%s>" % type_string(typeof(value))


func _input_map_patch(arguments: Dictionary) -> Dictionary:
	if not _only_keys(arguments, ["action", "deadzone", "add_events", "remove_events", "save", "dry_run"]):
		return _failure("input_map_patch contains an unsupported field")
	var action = arguments.get("action")
	if not action is String or action.is_empty() or action.length() > 128 or "/" in action:
		return _failure("Action must be a non-empty name up to 128 characters")
	var add_events = arguments.get("add_events", [])
	var remove_events = arguments.get("remove_events", [])
	var save = arguments.get("save", true)
	var dry_run = arguments.get("dry_run", false)
	if not add_events is Array or add_events.size() > MAX_INPUT_EVENTS:
		return _failure("add_events must be an array with at most 32 entries")
	if not remove_events is Array or remove_events.size() > MAX_INPUT_EVENTS:
		return _failure("remove_events must be an array with at most 32 entries")
	if not save is bool or not dry_run is bool:
		return _failure("save and dry_run must be booleans")
	var deadzone_value = arguments.get("deadzone", null)
	if deadzone_value != null and (not (deadzone_value is int or deadzone_value is float) or float(deadzone_value) < 0.0 or float(deadzone_value) > 1.0):
		return _failure("deadzone must be between 0 and 1")
	var added: Array[InputEvent] = []
	var removed: Array[Dictionary] = []
	for raw_event in add_events:
		var converted := _input_event_from_json(raw_event)
		if not converted.ok:
			return converted
		added.append(converted.result)
	for raw_event in remove_events:
		var converted := _input_event_from_json(raw_event)
		if not converted.ok:
			return converted
		removed.append(_normalize_input_event(converted.result))
	var setting_key := "input/%s" % action
	var existed := ProjectSettings.has_setting(setting_key)
	var before_setting = ProjectSettings.get_setting(setting_key) if existed else {"deadzone": 0.5, "events": []}
	if not before_setting is Dictionary or not before_setting.get("events", []) is Array:
		return _failure("Existing Input Map action has an unsupported format")
	var after_setting: Dictionary = before_setting.duplicate(true)
	var after_events: Array = after_setting.get("events", []).duplicate()
	var removed_count := 0
	for index in range(after_events.size() - 1, -1, -1):
		var normalized := _normalize_input_event(after_events[index])
		if normalized in removed:
			after_events.remove_at(index)
			removed_count += 1
	var added_count := 0
	for event in added:
		var normalized := _normalize_input_event(event)
		var duplicate := false
		for existing in after_events:
			if _normalize_input_event(existing) == normalized:
				duplicate = true
				break
		if not duplicate:
			after_events.append(event)
			added_count += 1
	after_setting["events"] = after_events
	if deadzone_value != null:
		after_setting["deadzone"] = float(deadzone_value)
	elif not after_setting.has("deadzone"):
		after_setting["deadzone"] = 0.5
	var before := _normalized_input_action(action, before_setting, existed)
	var after := _normalized_input_action(action, after_setting, true)
	if not dry_run:
		ProjectSettings.set_setting(setting_key, after_setting)
		InputMap.load_from_project_settings()
		if save:
			var error := ProjectSettings.save()
			if error != OK:
				if existed:
					ProjectSettings.set_setting(setting_key, before_setting)
				else:
					ProjectSettings.clear(setting_key)
				InputMap.load_from_project_settings()
				ProjectSettings.save()
				return _failure("Could not save Input Map (Godot error %d); transaction rolled back" % error)
	return _success({
		"diff": {"before": before, "after": after, "changed": before != after},
		"added": added_count,
		"removed": removed_count,
		"dry_run": dry_run,
		"saved": save and not dry_run,
		"requirements": {
			"editor_refresh": before != after,
			"project_reload": false,
			"editor_restart": false,
		},
	})


func _normalized_input_action(action: String, setting: Dictionary, exists: bool) -> Dictionary:
	var events: Array = []
	for event in setting.get("events", []):
		events.append(_normalize_input_event(event))
	return {
		"action": action,
		"exists": exists,
		"deadzone": float(setting.get("deadzone", 0.5)),
		"events": events,
	}


func _input_event_from_json(value: Variant) -> Dictionary:
	if not value is Dictionary:
		return _failure("Input events must be objects")
	if not _only_keys(value, ["type", "key", "physical", "button", "axis", "direction", "device", "shift", "alt", "ctrl", "meta"]):
		return _failure("Input event contains an unsupported field")
	var event_type = value.get("type")
	var device := _bounded_device(value.get("device", -1))
	if not device.ok:
		return device
	match event_type:
		"key":
			var code := _keycode(value.get("key"))
			if not code.ok:
				return code
			var physical = value.get("physical", false)
			if not physical is bool:
				return _failure("physical must be a boolean")
			var event := InputEventKey.new()
			event.device = device.result
			if physical:
				event.physical_keycode = code.result
			else:
				event.keycode = code.result
			for modifier in ["shift", "alt", "ctrl", "meta"]:
				if not value.get(modifier, false) is bool:
					return _failure("Key modifiers must be booleans")
			event.shift_pressed = value.get("shift", false)
			event.alt_pressed = value.get("alt", false)
			event.ctrl_pressed = value.get("ctrl", false)
			event.meta_pressed = value.get("meta", false)
			return _success(event)
		"mouse_button":
			var button := _named_index(value.get("button"), {
				"left": 1, "right": 2, "middle": 3, "wheel_up": 4,
				"wheel_down": 5, "wheel_left": 6, "wheel_right": 7,
				"xbutton1": 8, "xbutton2": 9,
			})
			if not button.ok or button.result < 1 or button.result > 9:
				return _failure("Mouse button is invalid")
			var event := InputEventMouseButton.new()
			event.device = device.result
			event.button_index = button.result
			return _success(event)
		"joypad_button":
			var button := _named_index(value.get("button"), {
				"a": 0, "b": 1, "x": 2, "y": 3, "back": 4, "guide": 5,
				"start": 6, "left_stick": 7, "right_stick": 8,
				"left_shoulder": 9, "right_shoulder": 10, "dpad_up": 11,
				"dpad_down": 12, "dpad_left": 13, "dpad_right": 14,
				"misc1": 15, "paddle1": 16, "paddle2": 17, "paddle3": 18,
				"paddle4": 19, "touchpad": 20,
			})
			if not button.ok or button.result < 0 or button.result > 20:
				return _failure("Joypad button is invalid")
			var event := InputEventJoypadButton.new()
			event.device = device.result
			event.button_index = button.result
			return _success(event)
		"joypad_motion":
			var axis := _named_index(value.get("axis"), {
				"left_x": 0, "left_y": 1, "right_x": 2, "right_y": 3,
				"trigger_left": 4, "trigger_right": 5,
			})
			var direction = value.get("direction")
			if not axis.ok or axis.result < 0 or axis.result > 5:
				return _failure("Joypad axis is invalid")
			if not (direction is int or direction is float) or float(direction) not in [-1.0, 1.0]:
				return _failure("Joypad motion direction must be -1 or 1")
			var event := InputEventJoypadMotion.new()
			event.device = device.result
			event.axis = axis.result
			event.axis_value = float(direction)
			return _success(event)
		_:
			return _failure("Input event type must be key, mouse_button, joypad_button, or joypad_motion")


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


func _keycode(value: Variant) -> Dictionary:
	if (value is int or value is float) and float(value) == floor(float(value)) and value > 0:
		return _success(int(value))
	if value is String and not value.is_empty() and value.length() <= 64:
		var code := OS.find_keycode_from_string(value)
		if code != 0:
			return _success(code)
	return _failure("Key must be a recognized name or positive Godot keycode")


func _bounded_device(value: Variant) -> Dictionary:
	if not (value is int or value is float) or float(value) != floor(float(value)) or value < -1 or value > 32:
		return _failure("Input device must be an integer between -1 and 32")
	return _success(int(value))


func _named_index(value: Variant, names: Dictionary) -> Dictionary:
	if (value is int or value is float) and float(value) == floor(float(value)):
		return _success(int(value))
	if value is String and value.to_lower() in names:
		return _success(names[value.to_lower()])
	return _failure("Named index is invalid")


func _only_keys(dictionary: Dictionary, allowed: Array) -> bool:
	for key in dictionary:
		if key not in allowed:
			return false
	return true


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
