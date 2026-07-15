extends RefCounted


static func read(path: String, max_bytes: int) -> Dictionary:
	if not FileAccess.file_exists(path):
		return {"ok": false, "error": "missing", "bytes": 0}
	var bytes := FileAccess.get_file_as_bytes(path)
	if bytes.is_empty() or bytes.size() > max_bytes:
		return {"ok": false, "error": "size", "bytes": bytes.size()}
	var value = JSON.parse_string(bytes.get_string_from_utf8())
	if value == null:
		return {"ok": false, "error": "parse", "bytes": bytes.size()}
	return {"ok": true, "value": value, "bytes": bytes.size()}


static func write(path: String, value: Variant) -> int:
	var temporary := path + ".tmp-%d" % OS.get_process_id()
	var file := FileAccess.open(temporary, FileAccess.WRITE)
	if file == null:
		return FileAccess.get_open_error()
	file.store_string(JSON.stringify(value) + "\n")
	file.close()
	var error := DirAccess.rename_absolute(
		ProjectSettings.globalize_path(temporary),
		ProjectSettings.globalize_path(path),
	)
	if error != OK:
		DirAccess.remove_absolute(ProjectSettings.globalize_path(temporary))
	return error
