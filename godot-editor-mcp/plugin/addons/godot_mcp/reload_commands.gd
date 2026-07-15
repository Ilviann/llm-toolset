extends RefCounted

const AtomicJsonRecord := preload("atomic_json_record.gd")
const ErrorEnvelope := preload("error_envelope.gd")
const ProjectIdentity := preload("project_identity.gd")

const RECORD_PATH := "res://.godot/godot_mcp_reload.json"
const RECORD_VERSION := 1
const MAX_RECORD_BYTES := 4096
const MAX_RECORD_AGE_MS := 300_000
const MAX_FUTURE_SKEW_MS := 10_000

var _editor_interface: EditorInterface
var _operations: RefCounted
var _bridge_version := ""
var _project_identity_hash := ""
var _restart_requested := false
var _cleanup_requested := false
var _pending_operation_id := ""
var _completed_reload: Dictionary = {}
var _recovery_error: Dictionary = {}


func _init(
	editor_interface: EditorInterface, operations: RefCounted, bridge_version: String,
) -> void:
	_editor_interface = editor_interface
	_operations = operations
	_bridge_version = bridge_version
	_project_identity_hash = ProjectIdentity.current_hash()
	_recover_pending()


func handlers() -> Dictionary:
	return {
		"reload_project": Callable(self, "_schedule"),
		"reload_status": Callable(self, "_status"),
	}


func poll() -> void:
	if _restart_requested:
		_restart_requested = false
		# The plugin calls this only after bridge polling has written the command
		# response. Passing false prevents Godot from silently saving or discarding
		# anything outside the explicit safeguards below.
		_editor_interface.restart_editor(false)
	elif _cleanup_requested:
		_cleanup_requested = false
		DirAccess.remove_absolute(ProjectSettings.globalize_path(RECORD_PATH))


func completion() -> Dictionary:
	return _completed_reload.duplicate(true)


func recovery_error() -> Dictionary:
	return _recovery_error.duplicate(true)


func _schedule(arguments: Dictionary) -> Dictionary:
	var unknown := _unknown_keys(arguments, ["stop_running", "save_scenes"])
	if not unknown.is_empty():
		return ErrorEnvelope.failure(
			"Unknown reload_project argument", ErrorEnvelope.INVALID_ARGUMENT,
			{"argument": unknown}, false,
		)
	var stop_running = arguments.get("stop_running", false)
	var save_scenes = arguments.get("save_scenes", false)
	if not (stop_running is bool) or not (save_scenes is bool):
		return ErrorEnvelope.failure(
			"stop_running and save_scenes must be booleans",
			ErrorEnvelope.INVALID_ARGUMENT,
		)
	if _restart_requested:
		return ErrorEnvelope.failure(
			"A project reload is already scheduled", ErrorEnvelope.EDITOR_BUSY,
			{"operation_id": _pending_operation_id}, true,
		)

	if _editor_interface.is_playing_scene():
		if not stop_running:
			return ErrorEnvelope.failure(
				"A scene is running; set stop_running to reload",
				ErrorEnvelope.EDITOR_BUSY, {}, false,
			)
		_editor_interface.stop_playing_scene()

	var dirty := _dirty_scenes()
	if not dirty.is_empty() and not save_scenes:
		return ErrorEnvelope.failure(
			"Unsaved scenes block project reload; set save_scenes to save them",
			ErrorEnvelope.EDITOR_BUSY, {"dirty_scenes": dirty}, false,
		)
	if not dirty.is_empty():
		_editor_interface.save_all_scenes()
		var remaining := _dirty_scenes()
		if not remaining.is_empty():
			return ErrorEnvelope.failure(
				"One or more scenes could not be saved",
				ErrorEnvelope.SAVE_FAILED, {"dirty_scenes": remaining}, false,
			)

	var operation_id: String = _operations.accept("reload_project", {
		"stop_running": stop_running,
		"save_scenes": save_scenes,
	})
	var record := {
		"record_version": RECORD_VERSION,
		"status": "pending",
		"operation_id": operation_id,
		"project_hash": _project_identity_hash,
		"bridge_version": _bridge_version,
		"created_unix_ms": int(Time.get_unix_time_from_system() * 1000.0),
	}
	var write_error := _write_record(record)
	if write_error != OK:
		_operations.complete(operation_id, {"record_error": write_error})
		return ErrorEnvelope.failure(
			"Could not persist the pending reload operation",
			ErrorEnvelope.SAVE_FAILED, {"error": write_error}, true,
		)
	_pending_operation_id = operation_id
	_restart_requested = true
	return ErrorEnvelope.success({
		"status": "scheduled",
		"operation_id": operation_id,
		"project_hash": _project_identity_hash,
		"bridge_version": _bridge_version,
	})


func _status(arguments: Dictionary) -> Dictionary:
	if arguments.size() != 1 or not arguments.has("operation_id"):
		return ErrorEnvelope.failure(
			"operation_id is required", ErrorEnvelope.INVALID_ARGUMENT,
		)
	var requested = arguments.operation_id
	if not (requested is String) or requested.is_empty() or requested.length() > 128:
		return ErrorEnvelope.failure(
			"operation_id is invalid", ErrorEnvelope.INVALID_ARGUMENT,
		)
	if _restart_requested and requested == _pending_operation_id:
		return ErrorEnvelope.success({
			"completed": false,
			"status": "pending",
			"operation_id": requested,
			"project_hash": _project_identity_hash,
			"bridge_version": _bridge_version,
		})
	if not _completed_reload.is_empty() and requested == _completed_reload.operation_id:
		# Cleanup is deferred until the plugin's next poll, after the bridge has
		# sent this completion response. A crash or bind failure therefore leaves
		# the bounded pending record available for another valid startup.
		_cleanup_requested = true
		return ErrorEnvelope.success(_completed_reload.duplicate(true))
	if not _recovery_error.is_empty():
		return ErrorEnvelope.failure(
			str(_recovery_error.message), str(_recovery_error.code),
			_recovery_error.get("details", {}), false,
		)
	return ErrorEnvelope.failure(
		"Reload operation is stale or unknown", ErrorEnvelope.STALE_OPERATION,
		{"operation_id": requested}, false,
	)


func _recover_pending() -> void:
	if not FileAccess.file_exists(RECORD_PATH):
		return
	var loaded := AtomicJsonRecord.read(RECORD_PATH, MAX_RECORD_BYTES)
	if not loaded.ok:
		_set_recovery_error(
			ErrorEnvelope.MALFORMED_OPERATION,
			"Pending reload operation record is malformed",
			{"bytes": loaded.bytes},
		)
		return
	var value = loaded.value
	var validation := validate_record(
		value, _project_identity_hash, _bridge_version,
		int(Time.get_unix_time_from_system() * 1000.0),
	)
	if not validation.ok:
		_set_recovery_error(validation.code, validation.message, validation.details)
		return
	var record: Dictionary = value
	_completed_reload = {
		"completed": true,
		"status": "completed",
		"operation_id": record.operation_id,
		"project_hash": _project_identity_hash,
		"bridge_version": _bridge_version,
		"recovered": true,
	}
	_operations.restore_completed(
		record.operation_id, "reload_project", {}, {"recovered": true},
	)


static func validate_record(
	value: Variant, expected_project_hash: String, expected_bridge_version: String,
	now_unix_ms: int,
) -> Dictionary:
	if not (value is Dictionary):
		return _invalid_record("Pending reload operation record must be an object")
	var record: Dictionary = value
	var required := [
		"record_version", "status", "operation_id", "project_hash",
		"bridge_version", "created_unix_ms",
	]
	if record.size() != required.size():
		return _invalid_record("Pending reload operation record has invalid fields")
	for key in required:
		if not record.has(key):
			return _invalid_record("Pending reload operation record is incomplete")
	if record.record_version != RECORD_VERSION or record.status != "pending":
		return {
			"ok": false,
			"code": ErrorEnvelope.STALE_OPERATION,
			"message": "Pending reload operation record is stale",
			"details": {},
		}
	if (
		not (record.operation_id is String)
		or record.operation_id.is_empty()
		or record.operation_id.length() > 128
		or not (record.project_hash is String)
		or record.project_hash.length() != 64
		or not (record.bridge_version is String)
		or record.bridge_version.is_empty()
		or (
			not (record.created_unix_ms is int)
			and not (record.created_unix_ms is float)
		)
		or float(record.created_unix_ms) != floorf(float(record.created_unix_ms))
		or float(record.created_unix_ms) <= 0.0
	):
		return _invalid_record("Pending reload operation record has invalid values")
	if record.project_hash != expected_project_hash:
		return {
			"ok": false,
			"code": ErrorEnvelope.PROJECT_MISMATCH,
			"message": "Pending reload operation belongs to another project",
			"details": {
				"expected_project_hash": expected_project_hash,
				"record_project_hash": record.project_hash,
			},
		}
	if record.bridge_version != expected_bridge_version:
		return {
			"ok": false,
			"code": ErrorEnvelope.VERSION_MISMATCH,
			"message": "Bridge version changed during project reload",
			"details": {
				"expected_bridge_version": record.bridge_version,
				"bridge_version": expected_bridge_version,
			},
		}
	var age: int = now_unix_ms - int(record.created_unix_ms)
	if age < -MAX_FUTURE_SKEW_MS or age > MAX_RECORD_AGE_MS:
		return {
			"ok": false,
			"code": ErrorEnvelope.STALE_OPERATION,
			"message": "Pending reload operation record is stale",
			"details": {"age_ms": age},
		}
	return {"ok": true}


static func project_hash() -> String:
	return ProjectIdentity.current_hash()


static func _invalid_record(message: String) -> Dictionary:
	return {
		"ok": false,
		"code": ErrorEnvelope.MALFORMED_OPERATION,
		"message": message,
		"details": {},
	}


func _dirty_scenes() -> Array[String]:
	var output: Array[String] = []
	for path in _editor_interface.get_unsaved_scenes():
		output.append(str(path).left(256))
		if output.size() >= 16:
			break
	return output


func _write_record(record: Dictionary) -> int:
	return AtomicJsonRecord.write(RECORD_PATH, record)


func _set_recovery_error(code: String, message: String, details: Dictionary) -> void:
	_recovery_error = {"code": code, "message": message, "details": details}


func _unknown_keys(arguments: Dictionary, allowed: Array[String]) -> String:
	for key in arguments:
		if not str(key) in allowed:
			return str(key).left(128)
	return ""
