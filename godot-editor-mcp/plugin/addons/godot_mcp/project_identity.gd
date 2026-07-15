extends RefCounted


static func normalized_path(path: String, platform_name := "") -> String:
	var normalized := path.replace("\\", "/").trim_suffix("/")
	var platform := OS.get_name() if platform_name.is_empty() else platform_name
	return normalized.to_lower() if platform == "Windows" else normalized


static func hash_path(path: String, platform_name := "") -> String:
	var context := HashingContext.new()
	context.start(HashingContext.HASH_SHA256)
	context.update(normalized_path(path, platform_name).to_utf8_buffer())
	return context.finish().hex_encode()


static func current_hash() -> String:
	return hash_path(ProjectSettings.globalize_path("res://"))
