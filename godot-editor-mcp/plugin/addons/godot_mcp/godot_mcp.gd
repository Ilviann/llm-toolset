@tool
extends EditorPlugin

const AssetCommands := preload("asset_commands.gd")
const BridgeServer := preload("bridge_server.gd")
const CommandRouter := preload("command_router.gd")
const DiscoveryRecord := preload("discovery_record.gd")
const DiagnosticStore := preload("diagnostic_store.gd")
const EditorStateMonitor := preload("editor_state_monitor.gd")
const ErrorEnvelope := preload("error_envelope.gd")
const EventStore := preload("event_store.gd")
const InputMapCommands := preload("input_map_commands.gd")
const Limits := preload("command_limits.gd")
const OperationRegistry := preload("operation_registry.gd")
const ProjectSettingsCommands := preload("project_settings_commands.gd")
const ReloadCommands := preload("reload_commands.gd")
const SceneCommands := preload("scene_commands.gd")

const BRIDGE_VERSION := "0.7.0"
const BRIDGE_PROTOCOL_VERSION := "1"
const DEFAULT_PORT := 6505
const TOKEN_PATH := "res://.godot/godot_mcp_token"

var _bridge_server
var _discovery
var _diagnostics
var _events
var _operations
var _router
var _reload_commands
var _state_monitor
var _port := DEFAULT_PORT


func _enter_tree() -> void:
	set_process(false)
	var token := _load_or_create_token()
	_port = int(ProjectSettings.get_setting("godot_mcp/port", DEFAULT_PORT))
	if _port < 1 or _port > 65535:
		push_error("Godot MCP bridge port must be between 1 and 65535")
		return

	_events = EventStore.new()
	_operations = OperationRegistry.new()
	_diagnostics = DiagnosticStore.new()
	OS.add_logger(_diagnostics)
	_state_monitor = EditorStateMonitor.new(
		get_editor_interface(), get_undo_redo(), _events, _operations, _diagnostics,
	)
	_reload_commands = ReloadCommands.new(
		get_editor_interface(), _operations, BRIDGE_VERSION,
	)
	if not scene_saved.is_connected(_on_scene_saved):
		scene_saved.connect(_on_scene_saved)
	_router = CommandRouter.new()
	_register_commands()

	_bridge_server = BridgeServer.new()
	var error: int = _bridge_server.start(_port, token, Callable(_router, "dispatch"))
	if error != OK:
		push_error(
			"Godot MCP bridge could not listen on 127.0.0.1:%d (error %d); " % [_port, error]
			+ "another editor may already own this port"
		)
		_shutdown_services()
		return

	_discovery = DiscoveryRecord.new()
	_discovery.start(_port, BRIDGE_VERSION)
	set_process(true)
	print("Godot MCP bridge listening on 127.0.0.1:%d" % _port)


func _exit_tree() -> void:
	set_process(false)
	if _discovery != null:
		_discovery.stop()
	if _bridge_server != null:
		_bridge_server.stop()
	_shutdown_services()


func _process(_delta: float) -> void:
	if _state_monitor != null:
		_state_monitor.poll()
	if _bridge_server != null:
		_bridge_server.poll()
	if _discovery != null:
		_discovery.poll()
	if _reload_commands != null:
		_reload_commands.poll()


func _register_commands() -> void:
	_router.register_handler("capabilities", Callable(self, "_capabilities"))
	_router.register_handler("state", Callable(self, "_editor_state"))
	_router.register_handler("diagnostics", Callable(_diagnostics, "read"))
	_router.register_handler("control", Callable(_state_monitor, "scene_control"))

	var asset_commands = AssetCommands.new(
		get_editor_interface(), get_undo_redo(), _operations, _state_monitor,
	)
	var scene_commands = SceneCommands.new(get_editor_interface(), get_undo_redo(), _operations)
	var settings_commands = ProjectSettingsCommands.new(
		get_editor_interface(), get_undo_redo(), _operations, _state_monitor,
	)
	var input_commands = InputMapCommands.new(
		get_editor_interface(), get_undo_redo(), _operations, _state_monitor,
	)
	_router.register_service(
		["assets", "asset_info", "scan_asset", "create_resource", "create_scene", "open_scene"],
		asset_commands,
	)
	_router.register_service(
		["tree", "inspect", "add_node", "instantiate_scene", "set_property", "select"],
		scene_commands,
	)
	_router.register_service(
		["project_settings_get", "project_settings_patch"], settings_commands,
	)
	_router.register_handler("input_map_patch", Callable(input_commands, "execute"))
	_router.register_service(["reload_project", "reload_status"], _reload_commands)


func _capabilities(_arguments: Dictionary) -> Dictionary:
	return ErrorEnvelope.success({
		"bridge_version": BRIDGE_VERSION,
		"bridge_protocol_version": BRIDGE_PROTOCOL_VERSION,
		"godot_version": str(Engine.get_version_info().get("string", "Godot 4")),
		"commands": _router.commands(),
		"features": {
			"runtime_inspection": false,
			"game_view_capture": false,
			"input_injection": false,
			"diagnostics": true,
			"gdscript_diagnostics": true,
			"csharp_diagnostics": false,
			"runtime_diagnostics": true,
			"project_settings": true,
			"input_map_editing": true,
			"structured_errors": true,
			"operation_ids": true,
			"event_ids": true,
			"project_discovery": true,
			"project_reload": true,
		},
		"error_codes": [
			ErrorEnvelope.UNAUTHORIZED,
			ErrorEnvelope.INVALID_ARGUMENT,
			ErrorEnvelope.PROTECTED_PATH,
			ErrorEnvelope.NOT_FOUND,
			ErrorEnvelope.EDITOR_BUSY,
			ErrorEnvelope.IMPORT_PENDING,
			ErrorEnvelope.NO_ACTIVE_RUN,
			ErrorEnvelope.STALE_RUNTIME_ID,
			ErrorEnvelope.TIMEOUT,
			ErrorEnvelope.UNSUPPORTED_CAPABILITY,
			ErrorEnvelope.STALE_CURSOR,
			ErrorEnvelope.PROJECT_MISMATCH,
			ErrorEnvelope.SAVE_FAILED,
			ErrorEnvelope.MALFORMED_OPERATION,
			ErrorEnvelope.STALE_OPERATION,
			ErrorEnvelope.VERSION_MISMATCH,
		],
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
			"response_bytes": Limits.MAX_RESPONSE_BYTES,
			"tree_nodes": Limits.MAX_TREE_NODES,
			"properties": Limits.MAX_PROPERTIES,
			"assets": Limits.MAX_ASSETS,
			"asset_scan": Limits.MAX_ASSET_SCAN,
			"settings": Limits.MAX_SETTINGS,
			"setting_changes": Limits.MAX_SETTING_CHANGES,
			"input_events": Limits.MAX_INPUT_EVENTS,
			"diagnostics": Limits.MAX_DIAGNOSTICS,
			"diagnostic_records": Limits.MAX_DIAGNOSTIC_RECORDS,
		},
	})


func _editor_state(_arguments: Dictionary) -> Dictionary:
	var state: Dictionary = _state_monitor.state()
	state["bridge_version"] = BRIDGE_VERSION
	state["bridge_protocol_version"] = BRIDGE_PROTOCOL_VERSION
	state["bridge_port"] = _port
	return ErrorEnvelope.success(state)


func _shutdown_services() -> void:
	if scene_saved.is_connected(_on_scene_saved):
		scene_saved.disconnect(_on_scene_saved)
	if _state_monitor != null:
		_state_monitor.stop()
	if _diagnostics != null:
		OS.remove_logger(_diagnostics)
	_bridge_server = null
	_discovery = null
	_diagnostics = null
	_events = null
	_operations = null
	_router = null
	_reload_commands = null
	_state_monitor = null


func _on_scene_saved(_path: String) -> void:
	if _state_monitor != null:
		_state_monitor.mark_scene_saved()


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
