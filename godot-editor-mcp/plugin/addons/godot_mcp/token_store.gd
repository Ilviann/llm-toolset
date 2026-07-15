extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")

const TOKEN_BYTES := 32
const TOKEN_CHARS := TOKEN_BYTES * 2

var _reader: Callable
var _writer: Callable
var _generator: Callable


func _init(
	reader := Callable(), writer := Callable(), generator := Callable(),
) -> void:
	_reader = reader
	_writer = writer
	_generator = generator


func load_or_create(path: String) -> Dictionary:
	var read_result: Dictionary = (
		_reader.call(path) if _reader.is_valid() else _read_file(path)
	)
	if not read_result.get("ok", false):
		return ErrorEnvelope.failure(
			"Godot MCP bridge could not read its token",
			ErrorEnvelope.SAVE_FAILED,
			{"path": path}, false,
		)
	if bool(read_result.get("exists", false)):
		var existing := str(read_result.get("text", "")).strip_edges().to_lower()
		if existing.length() == TOKEN_CHARS and existing.is_valid_hex_number():
			return ErrorEnvelope.success(existing)
	var token := str(
		_generator.call() if _generator.is_valid()
		else Crypto.new().generate_random_bytes(TOKEN_BYTES).hex_encode()
	).to_lower()
	if token.length() != TOKEN_CHARS or not token.is_valid_hex_number():
		return ErrorEnvelope.failure(
			"Godot MCP bridge could not generate a valid token",
			ErrorEnvelope.SAVE_FAILED,
		)
	var write_error: int = int(
		_writer.call(path, token) if _writer.is_valid() else _write_file(path, token)
	)
	if write_error != OK:
		return ErrorEnvelope.failure(
			"Godot MCP bridge could not persist its token",
			ErrorEnvelope.SAVE_FAILED,
			{"path": path, "error": write_error}, false,
		)
	return ErrorEnvelope.success(token)


func _read_file(path: String) -> Dictionary:
	if not FileAccess.file_exists(path):
		return {"ok": true, "exists": false, "text": ""}
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {"ok": false, "exists": true, "text": ""}
	var text := file.get_as_text()
	var read_error := file.get_error()
	return {"ok": read_error == OK, "exists": true, "text": text}


func _write_file(path: String, token: String) -> int:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return FileAccess.get_open_error()
	file.store_string(token + "\n")
	file.flush()
	return file.get_error()
