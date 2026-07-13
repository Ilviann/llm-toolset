@tool
extends EditorPlugin

const AssetCommands := preload("asset_commands.gd")
const InputMapCommands := preload("input_map_commands.gd")
const Limits := preload("command_limits.gd")
const ProjectSettingsCommands := preload("project_settings_commands.gd")
const SceneCommands := preload("scene_commands.gd")

const HOST := "127.0.0.1"
const BRIDGE_VERSION := "0.4.1"
const DEFAULT_PORT := 6505
const TOKEN_PATH := "res://.godot/godot_mcp_token"

var _server := TCPServer.new()
var _clients: Array[Dictionary] = []
var _token := ""
var _port := DEFAULT_PORT
var _filesystem_generation := 0
var _run_id := 0
var _was_playing := false
var _last_run_exit_status := "never_started"
var _last_stop_reason := ""
var _asset_commands
var _input_map_commands
var _project_settings_commands
var _scene_commands


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
	_asset_commands = AssetCommands.new(get_editor_interface(), get_undo_redo())
	_input_map_commands = InputMapCommands.new(get_editor_interface(), get_undo_redo())
	_project_settings_commands = ProjectSettingsCommands.new(get_editor_interface(), get_undo_redo())
	_scene_commands = SceneCommands.new(get_editor_interface(), get_undo_redo())
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
	_asset_commands = null
	_input_map_commands = null
	_project_settings_commands = null
	_scene_commands = null


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
		if buffer.size() > Limits.MAX_REQUEST_BYTES:
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
		"assets", "asset_info", "scan_asset", "create_resource", "create_scene", "open_scene":
			return _asset_commands.execute(command, arguments)
		"tree", "inspect", "add_node", "instantiate_scene", "set_property", "select":
			return _scene_commands.execute(command, arguments)
		"control":
			return _scene_control(arguments)
		"project_settings_get", "project_settings_patch":
			return _project_settings_commands.execute(command, arguments)
		"input_map_patch":
			return _input_map_commands.execute(arguments)
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
			"request_bytes": Limits.MAX_REQUEST_BYTES,
			"tree_nodes": Limits.MAX_TREE_NODES,
			"properties": Limits.MAX_PROPERTIES,
			"assets": Limits.MAX_ASSETS,
			"asset_scan": Limits.MAX_ASSET_SCAN,
			"settings": Limits.MAX_SETTINGS,
			"setting_changes": Limits.MAX_SETTING_CHANGES,
			"input_events": Limits.MAX_INPUT_EVENTS,
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
