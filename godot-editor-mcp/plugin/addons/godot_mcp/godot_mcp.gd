@tool
extends EditorPlugin

const AssetCommands := preload("asset_commands.gd")
const BridgeServer := preload("bridge_server.gd")
const CommandRouter := preload("command_router.gd")
const CursorStore := preload("cursor_store.gd")
const DiscoveryRecord := preload("discovery_record.gd")
const DiagnosticStore := preload("diagnostic_store.gd")
const EditedSceneInspector := preload("edited_scene_inspector.gd")
const EditorStateMonitor := preload("editor_state_monitor.gd")
const ErrorEnvelope := preload("error_envelope.gd")
const EventStore := preload("event_store.gd")
const ImportStateTracker := preload("import_state_tracker.gd")
const InputMapCommands := preload("input_map_commands.gd")
const InputEventCodec := preload("input_event_codec.gd")
const Limits := preload("command_limits.gd")
const OperationRegistry := preload("operation_registry.gd")
const ProjectPathGuard := preload("project_path_guard.gd")
const ProjectFileStateTracker := preload("project_file_state_tracker.gd")
const ProjectIdentity := preload("project_identity.gd")
const ProjectSettingsCommands := preload("project_settings_commands.gd")
const PropertyValueCodec := preload("property_value_codec.gd")
const ReloadCommands := preload("reload_commands.gd")
const RunStateTracker := preload("run_state_tracker.gd")
const RuntimeDebuggerGateway := preload("runtime_debugger_gateway.gd")
const RuntimeGameplayCommands := preload("runtime_gameplay_commands.gd")
const RuntimeSceneInspector := preload("runtime_scene_inspector.gd")
const SceneCommands := preload("scene_commands.gd")
const SceneNodeAccess := preload("scene_node_access.gd")
const SceneStateTracker := preload("scene_state_tracker.gd")

const BRIDGE_VERSION := "0.14.0"
const BRIDGE_PROTOCOL_VERSION := "1"
const DEFAULT_PORT := 6505
const TOKEN_PATH := "res://.godot/godot_mcp_token"
const RUNTIME_PROBE_AUTOLOAD := "GodotMCPRuntimeProbe"
const RUNTIME_PROBE_PATH := "res://addons/godot_mcp/runtime_probe.gd"
const RUNTIME_PROBE_VERSION := "2"

var _bridge_server
var _command_services: Array = []
var _discovery
var _diagnostics
var _cursors
var _events
var _import_state
var _operations
var _project_file_state
var _router
var _reload_commands
var _runtime_debugger
var _runtime_probe_available := false
var _runtime_probe_owned := false
var _runtime_probe_uid := ""
var _run_state
var _scene_state
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
	_cursors = CursorStore.new()
	_diagnostics = DiagnosticStore.new()
	OS.add_logger(_diagnostics)
	_scene_state = SceneStateTracker.new(
		get_editor_interface(), get_undo_redo(), _events, _operations,
	)
	_run_state = RunStateTracker.new(
		get_editor_interface(), _events, _operations, _diagnostics,
	)
	_install_runtime_plane()
	_import_state = ImportStateTracker.new(
		get_editor_interface(), _events, _operations, _diagnostics,
	)
	_project_file_state = ProjectFileStateTracker.new()
	_state_monitor = EditorStateMonitor.new(
		_events, _operations, _diagnostics,
		_scene_state, _run_state, _import_state, _project_file_state,
	)
	_reload_commands = ReloadCommands.new(
		get_editor_interface(), _operations, BRIDGE_VERSION,
	)
	if not scene_saved.is_connected(_on_scene_saved):
		scene_saved.connect(_on_scene_saved)
	_router = CommandRouter.new()
	if not _register_commands():
		_shutdown_services()
		return

	_bridge_server = BridgeServer.new()
	var error: int = _bridge_server.start(
		_port, token, Callable(_router, "dispatch"),
		Callable(_runtime_debugger, "take_response"),
	)
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
	if _runtime_debugger != null:
		_runtime_debugger.poll()
	if _discovery != null:
		_discovery.poll()
	if _reload_commands != null:
		_reload_commands.poll()


func _register_commands() -> bool:
	var project_paths = ProjectPathGuard.new()
	var scene_nodes = SceneNodeAccess.new(get_editor_interface())
	var property_values = PropertyValueCodec.new()
	var input_events = InputEventCodec.new()
	var asset_commands = AssetCommands.new(
		get_editor_interface(), _operations,
		Callable(_import_state, "track_import"),
		Callable(_import_state, "filesystem_generation"),
		project_paths, scene_nodes, property_values, _cursors,
	)
	var scene_commands = SceneCommands.new(
		get_editor_interface(), get_undo_redo(), project_paths, scene_nodes,
		property_values,
	)
	var edited_scene_inspector = EditedSceneInspector.new(
		get_editor_interface(), get_undo_redo(), scene_nodes, property_values, _cursors,
		RuntimeSceneInspector.new(_runtime_debugger, _cursors),
	)
	var settings_commands = ProjectSettingsCommands.new(
		Callable(_project_file_state, "mark_saved"), input_events,
	)
	var input_commands = InputMapCommands.new(
		Callable(_project_file_state, "mark_saved"), input_events,
	)
	var gameplay_commands = RuntimeGameplayCommands.new(
		_runtime_debugger, Callable(_run_state, "current_run_id"),
	)
	_command_services = [
		asset_commands, edited_scene_inspector, scene_commands,
		settings_commands, input_commands, gameplay_commands, _reload_commands,
	]
	if not _register_handlers("plugin", {
		"capabilities": Callable(self, "_capabilities"),
		"state": Callable(self, "_editor_state"),
		"diagnostics": Callable(_diagnostics, "read"),
		"control": Callable(_state_monitor, "scene_control"),
	}):
		return false
	for service in _command_services:
		if not _register_handlers(service.get_script().resource_path.get_file(), service.handlers()):
			return false
	return true


func _register_handlers(owner: String, handlers: Dictionary) -> bool:
	var registration: Dictionary = _router.register_handlers(owner, handlers)
	if registration.ok:
		return true
	push_error("Godot MCP command registration failed: %s" % ErrorEnvelope.message(registration))
	return false


func _capabilities(_arguments: Dictionary) -> Dictionary:
	var runtime_probe_status: Dictionary = _runtime_debugger.status()
	runtime_probe_status["autoload"] = RUNTIME_PROBE_AUTOLOAD
	runtime_probe_status["available"] = _runtime_probe_available
	runtime_probe_status["inert_without_debugger"] = true
	return ErrorEnvelope.success({
		"bridge_version": BRIDGE_VERSION,
		"bridge_protocol_version": BRIDGE_PROTOCOL_VERSION,
		"godot_version": str(Engine.get_version_info().get("string", "Godot 4")),
		"commands": _router.commands(),
		"features": {
			"runtime_inspection": _runtime_probe_available,
			"game_view_capture": _runtime_probe_available,
			"input_injection": _runtime_probe_available,
			"runtime_conditions": _runtime_probe_available,
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
			"targeted_inspection": true,
			"stable_pagination": true,
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
			ErrorEnvelope.RUNTIME_PROBE_UNAVAILABLE,
			ErrorEnvelope.AMBIGUOUS_RUNTIME_SESSION,
		],
		"runtime_probe": runtime_probe_status,
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
			"tree_depth": Limits.MAX_TREE_DEPTH,
			"tree_scan": Limits.MAX_TREE_SCAN,
			"properties": Limits.MAX_PROPERTIES,
			"property_scan": Limits.MAX_PROPERTY_SCAN,
			"assets": Limits.MAX_ASSETS,
			"asset_scan": Limits.MAX_ASSET_SCAN,
			"active_cursors": Limits.MAX_ACTIVE_CURSORS,
			"cursor_chars": Limits.MAX_CURSOR_CHARS,
			"cursor_ttl_ms": Limits.CURSOR_TTL_MSEC,
			"settings": Limits.MAX_SETTINGS,
			"setting_changes": Limits.MAX_SETTING_CHANGES,
			"input_events": Limits.MAX_INPUT_EVENTS,
			"diagnostics": Limits.MAX_DIAGNOSTICS,
			"diagnostic_records": Limits.MAX_DIAGNOSTIC_RECORDS,
			"runtime_pending_requests": Limits.MAX_RUNTIME_PENDING_REQUESTS,
			"runtime_request_timeout_ms": Limits.RUNTIME_REQUEST_TIMEOUT_MSEC,
			"runtime_groups": Limits.MAX_RUNTIME_GROUPS,
			"capture_source_width": Limits.MAX_CAPTURE_SOURCE_WIDTH,
			"capture_source_height": Limits.MAX_CAPTURE_SOURCE_HEIGHT,
			"capture_source_pixels": Limits.MAX_CAPTURE_SOURCE_PIXELS,
			"capture_output_width": Limits.MAX_CAPTURE_OUTPUT_WIDTH,
			"capture_output_height": Limits.MAX_CAPTURE_OUTPUT_HEIGHT,
			"capture_output_pixels": Limits.MAX_CAPTURE_OUTPUT_PIXELS,
			"capture_bytes": Limits.MAX_CAPTURE_BYTES,
			"capture_timeout_ms": Limits.CAPTURE_TIMEOUT_MSEC,
			"concurrent_inputs": Limits.MAX_CONCURRENT_INPUTS,
			"input_duration_ms": Limits.MAX_INPUT_DURATION_MSEC,
			"input_frames": Limits.MAX_INPUT_FRAMES,
			"condition_timeout_ms": Limits.MAX_CONDITION_TIMEOUT_MSEC,
			"condition_evidence": Limits.MAX_CONDITION_EVIDENCE,
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
	if _runtime_debugger != null:
		_runtime_debugger.stop()
		remove_debugger_plugin(_runtime_debugger)
	_remove_runtime_probe()
	_bridge_server = null
	_command_services.clear()
	_discovery = null
	_diagnostics = null
	if _cursors != null:
		_cursors.clear()
	_cursors = null
	_events = null
	_import_state = null
	_operations = null
	_project_file_state = null
	_router = null
	_reload_commands = null
	_runtime_debugger = null
	_run_state = null
	_scene_state = null
	_state_monitor = null


func _install_runtime_plane() -> void:
	_runtime_probe_uid = ResourceUID.path_to_uid(RUNTIME_PROBE_PATH)
	_runtime_debugger = RuntimeDebuggerGateway.new(
		Callable(_run_state, "current_run_id"), ProjectIdentity.current_hash(),
		RUNTIME_PROBE_VERSION,
	)
	add_debugger_plugin(_runtime_debugger)
	var setting := "autoload/" + RUNTIME_PROBE_AUTOLOAD
	if not ProjectSettings.has_setting(setting):
		add_autoload_singleton(RUNTIME_PROBE_AUTOLOAD, RUNTIME_PROBE_PATH)
		var save_error := ProjectSettings.save()
		if save_error != OK:
			push_error("Godot MCP could not persist its runtime probe autoload")
			ProjectSettings.set_setting(setting, null)
			_runtime_probe_available = false
			return
		_runtime_probe_owned = true
		_runtime_probe_available = true
		return
	if _runtime_probe_matches(ProjectSettings.get_setting(setting, "")):
		_runtime_probe_owned = true
		_runtime_probe_available = true
		return
	_runtime_probe_available = false
	push_warning(
		"Godot MCP runtime inspection is disabled because autoload %s already exists"
		% RUNTIME_PROBE_AUTOLOAD
	)


func _remove_runtime_probe() -> void:
	if _runtime_probe_owned:
		var setting := "autoload/" + RUNTIME_PROBE_AUTOLOAD
		if (
			ProjectSettings.has_setting(setting)
			and _runtime_probe_matches(ProjectSettings.get_setting(setting, ""))
		):
			# Avoid resolving a uid:// value while Godot's UID cache is shutting down.
			ProjectSettings.set_setting(setting, null)
			ProjectSettings.save()
	_runtime_probe_owned = false
	_runtime_probe_available = false


func _runtime_probe_matches(value: Variant) -> bool:
	var path := str(value).trim_prefix("*")
	return path == RUNTIME_PROBE_PATH or path == _runtime_probe_uid


func _on_scene_saved(_path: String) -> void:
	if _scene_state != null:
		_scene_state.mark_saved()


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
