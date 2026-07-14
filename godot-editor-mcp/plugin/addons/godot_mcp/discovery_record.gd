extends RefCounted

const FILE_PATH := "res://.godot/godot_mcp_bridge.json"
const PROTOCOL_VERSION := "1"
const HEARTBEAT_INTERVAL_MS := 1000

var _port := 0
var _bridge_version := ""
var _project_hash := ""
var _last_write_ticks := 0


func start(port: int, bridge_version: String) -> void:
	_port = port
	_bridge_version = bridge_version
	_project_hash = _hash_project_path()
	_write()


func poll() -> void:
	var now := Time.get_ticks_msec()
	if now - _last_write_ticks >= HEARTBEAT_INTERVAL_MS:
		_write()


func stop() -> void:
	var existing = JSON.parse_string(FileAccess.get_file_as_string(FILE_PATH))
	if existing is Dictionary and int(existing.get("process_id", -1)) == OS.get_process_id():
		DirAccess.remove_absolute(ProjectSettings.globalize_path(FILE_PATH))


func _write() -> void:
	var record := {
		"process_id": OS.get_process_id(),
		"project_hash": _project_hash,
		"port": _port,
		"bridge_version": _bridge_version,
		"protocol_version": PROTOCOL_VERSION,
		"heartbeat_unix_ms": int(Time.get_unix_time_from_system() * 1000.0),
	}
	var temporary := FILE_PATH + ".tmp-%d" % OS.get_process_id()
	var file := FileAccess.open(temporary, FileAccess.WRITE)
	if file == null:
		push_error("Godot MCP bridge could not write its discovery record")
		return
	file.store_string(JSON.stringify(record) + "\n")
	file.close()
	var error := DirAccess.rename_absolute(
		ProjectSettings.globalize_path(temporary),
		ProjectSettings.globalize_path(FILE_PATH),
	)
	if error != OK:
		DirAccess.remove_absolute(ProjectSettings.globalize_path(temporary))
		push_error("Godot MCP bridge could not publish its discovery record (error %d)" % error)
		return
	_last_write_ticks = Time.get_ticks_msec()


func _hash_project_path() -> String:
	var path := ProjectSettings.globalize_path("res://").replace("\\", "/").trim_suffix("/")
	if OS.get_name() == "Windows":
		path = path.to_lower()
	var context := HashingContext.new()
	context.start(HashingContext.HASH_SHA256)
	context.update(path.to_utf8_buffer())
	return context.finish().hex_encode()
