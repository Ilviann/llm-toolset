extends RefCounted

const AtomicJsonRecord := preload("atomic_json_record.gd")
const ProjectIdentity := preload("project_identity.gd")
const FILE_PATH := "res://.godot/godot_mcp_bridge.json"
const PROTOCOL_VERSION := "1"
const HEARTBEAT_INTERVAL_MS := 1000
const MAX_RECORD_BYTES := 4096

var _port := 0
var _bridge_version := ""
var _project_identity_hash := ""
var _last_write_ticks := 0


func start(port: int, bridge_version: String) -> void:
	_port = port
	_bridge_version = bridge_version
	_project_identity_hash = ProjectIdentity.current_hash()
	_write()


func poll() -> void:
	var now := Time.get_ticks_msec()
	if now - _last_write_ticks >= HEARTBEAT_INTERVAL_MS:
		_write()


func stop() -> void:
	var loaded := AtomicJsonRecord.read(FILE_PATH, MAX_RECORD_BYTES)
	var existing = loaded.get("value") if loaded.ok else null
	if existing is Dictionary and int(existing.get("process_id", -1)) == OS.get_process_id():
		DirAccess.remove_absolute(ProjectSettings.globalize_path(FILE_PATH))


func _write() -> void:
	var record := {
		"process_id": OS.get_process_id(),
		"project_hash": _project_identity_hash,
		"port": _port,
		"bridge_version": _bridge_version,
		"protocol_version": PROTOCOL_VERSION,
		"heartbeat_unix_ms": int(Time.get_unix_time_from_system() * 1000.0),
	}
	var error := AtomicJsonRecord.write(FILE_PATH, record)
	if error != OK:
		push_error("Godot MCP bridge could not publish its discovery record (error %d)" % error)
		return
	_last_write_ticks = Time.get_ticks_msec()
