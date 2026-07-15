extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")


func check(
	path_value: Variant,
	writable := false,
	allowed_extensions := PackedStringArray(),
) -> Dictionary:
	if not path_value is String or path_value.is_empty() or path_value.length() > 512:
		return ErrorEnvelope.failure("Project path must be a non-empty string up to 512 characters")
	var path := path_value as String
	if path == "." or path.begins_with("/") or path.begins_with("res://") or "\\" in path or "//" in path:
		return ErrorEnvelope.failure("Project path must be relative and cannot contain res://, . or ..")
	var parts := path.split("/")
	if "." in parts or ".." in parts:
		return ErrorEnvelope.failure("Project path must be relative and cannot contain res://, . or ..")
	if writable and parts[0].to_lower() in [".godot", "addons"]:
		return ErrorEnvelope.failure("Destination is a protected project folder")
	if not allowed_extensions.is_empty() and path.get_extension().to_lower() not in allowed_extensions:
		return ErrorEnvelope.failure("Project path has an unsupported extension")
	if _has_link(path):
		return ErrorEnvelope.failure("Project path cannot contain symbolic links")
	return ErrorEnvelope.success("res://" + path)


func _has_link(relative_path: String) -> bool:
	var current := "res://"
	for part in relative_path.split("/"):
		var directory := DirAccess.open(current)
		if directory != null and directory.is_link(part):
			return true
		current = current.path_join(part)
	return false
