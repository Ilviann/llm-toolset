extends RefCounted

var _hash_reader: Callable
var _project_file_hash := ""
var _known_project_file_hash := ""
var _project_reload_required := false
var _next_hash_check_ms := 0


func _init(hash_reader := Callable()) -> void:
	_hash_reader = hash_reader
	if not _hash_reader.is_valid():
		_hash_reader = Callable(self, "_hash_project_file")
	_project_file_hash = str(_hash_reader.call())
	_known_project_file_hash = _project_file_hash


func poll() -> void:
	if Time.get_ticks_msec() >= _next_hash_check_ms:
		_check_hash()
		_next_hash_check_ms = Time.get_ticks_msec() + 1000


func state() -> Dictionary:
	_check_hash()
	return {
		"project_file_hash": _project_file_hash,
		"project_file_hash_matches_known_write": (
			_project_file_hash == _known_project_file_hash
		),
		"project_reload_required": _project_reload_required,
	}


func mark_saved(reload_required: bool) -> void:
	_project_file_hash = str(_hash_reader.call())
	_known_project_file_hash = _project_file_hash
	_project_reload_required = _project_reload_required or reload_required


func _hash_project_file() -> String:
	var bytes := FileAccess.get_file_as_bytes("res://project.godot")
	if bytes.is_empty():
		return ""
	var context := HashingContext.new()
	context.start(HashingContext.HASH_SHA256)
	context.update(bytes)
	return context.finish().hex_encode()


func _check_hash() -> void:
	var current := str(_hash_reader.call())
	if current != _project_file_hash:
		_project_file_hash = current
		if current != _known_project_file_hash:
			_project_reload_required = true
