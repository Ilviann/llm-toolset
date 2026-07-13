extends "command_base.gd"

const Limits := preload("command_limits.gd")
const MAX_SETTINGS := Limits.MAX_SETTINGS
const MAX_SETTING_CHANGES := Limits.MAX_SETTING_CHANGES


func execute(command: String, arguments: Dictionary) -> Dictionary:
	match command:
		"project_settings_get":
			return _project_settings_get(arguments)
		"project_settings_patch":
			return _project_settings_patch(arguments)
		_:
			return _failure("Unknown project settings command")


func _project_settings_get(arguments: Dictionary) -> Dictionary:
	if not _only_keys(arguments, ["key", "recursive"]):
		return _failure("project_settings_get contains an unsupported field")
	var checked := _checked_setting_key(arguments.get("key"), false)
	if not checked.ok:
		return checked
	var key := checked.result as String
	var recursive = arguments.get("recursive", false)
	if not recursive is bool:
		return _failure("recursive must be a boolean")
	if not recursive:
		return _success(_setting_record(key))
	var settings: Array[Dictionary] = []
	var seen := {}
	for info in ProjectSettings.get_property_list():
		var candidate := str(info.get("name", ""))
		if candidate in seen or not (candidate == key or candidate.begins_with(key + "/")):
			continue
		if not _checked_setting_key(candidate, false).ok:
			continue
		seen[candidate] = true
		settings.append(_setting_record(candidate))
		if settings.size() >= MAX_SETTINGS:
			break
	settings.sort_custom(func(left: Dictionary, right: Dictionary) -> bool: return left.key < right.key)
	return _success({
		"prefix": key,
		"settings": settings,
		"truncated": settings.size() >= MAX_SETTINGS,
	})


func _setting_record(key: String) -> Dictionary:
	var exists := ProjectSettings.has_setting(key)
	var value = ProjectSettings.get_setting(key) if exists else null
	var has_default := exists and ProjectSettings.property_can_revert(key)
	var default_value = ProjectSettings.property_get_revert(key) if has_default else null
	return {
		"key": key,
		"exists": exists,
		"value": _encode_setting_value(value),
		"type": type_string(typeof(value)) if exists else "nil",
		"has_default": has_default,
		"default": _encode_setting_value(default_value) if has_default else null,
		"differs_from_default": exists and has_default and value != default_value,
		"reload": _setting_reload_requirement(key),
	}


func _project_settings_patch(arguments: Dictionary) -> Dictionary:
	if not _only_keys(arguments, ["changes", "save", "dry_run"]):
		return _failure("project_settings_patch contains an unsupported field")
	var changes = arguments.get("changes")
	if not changes is Array or changes.is_empty() or changes.size() > MAX_SETTING_CHANGES:
		return _failure("changes must contain between 1 and 32 entries")
	var save = arguments.get("save", true)
	var dry_run = arguments.get("dry_run", false)
	if not save is bool or not dry_run is bool:
		return _failure("save and dry_run must be booleans")
	var prepared: Array[Dictionary] = []
	var keys := {}
	for change in changes:
		if not change is Dictionary:
			return _failure("Each change must be an object")
		if not _only_keys(change, ["key", "expected", "value"]):
			return _failure("A change contains an unsupported field")
		if not change.has("value"):
			return _failure("Each change must include value")
		var checked := _checked_setting_key(change.get("key"), true)
		if not checked.ok:
			return checked
		var key := checked.result as String
		if key in keys:
			return _failure("Duplicate setting key: %s" % key)
		keys[key] = true
		var existed := ProjectSettings.has_setting(key)
		var before = ProjectSettings.get_setting(key) if existed else null
		if change.has("expected"):
			if not existed:
				if change.expected != null:
					return _failure("Compare-and-swap failed for %s" % key)
			else:
				var expected := _decode_setting_value(change.expected, typeof(before))
				if not expected.ok:
					return _failure("Invalid expected value for %s: %s" % [key, expected.error])
				if expected.result != before:
					return _failure("Compare-and-swap failed for %s" % key)
		var converted := _decode_setting_value(change.value, typeof(before) if existed else TYPE_NIL)
		if not converted.ok:
			return _failure("Invalid value for %s: %s" % [key, converted.error])
		prepared.append({
			"key": key, "existed": existed, "before_raw": before,
			"after_raw": converted.result,
			"diff": {
				"key": key,
				"before": _encode_setting_value(before) if existed else null,
				"after": _encode_setting_value(converted.result),
				"changed": not existed or before != converted.result,
				"reload": _setting_reload_requirement(key),
			},
		})
	var diffs: Array[Dictionary] = []
	for item in prepared:
		diffs.append(item.diff)
	if not dry_run:
		for item in prepared:
			ProjectSettings.set_setting(item.key, item.after_raw)
			if save:
				var error := ProjectSettings.save()
				if error != OK:
					_restore_settings(prepared)
					ProjectSettings.save()
					return _failure("Could not save project settings (Godot error %d); transaction rolled back" % error)
	return _success({
		"diff": diffs,
		"dry_run": dry_run,
		"saved": save and not dry_run,
		"requirements": _combined_reload_requirements(prepared),
	})


func _restore_settings(prepared: Array[Dictionary]) -> void:
	for item in prepared:
		if item.existed:
			ProjectSettings.set_setting(item.key, item.before_raw)
		else:
			ProjectSettings.clear(item.key)


func _combined_reload_requirements(prepared: Array[Dictionary]) -> Dictionary:
	var reload := false
	var restart := false
	for item in prepared:
		if not item.diff.changed:
			continue
		var requirement := item.diff.reload as String
		reload = reload or requirement == "project_reload"
		restart = restart or requirement == "editor_restart"
	return {
		"editor_refresh": false,
		"project_reload": reload,
		"editor_restart": restart,
	}


func _setting_reload_requirement(key: String) -> String:
	if key.begins_with("godot_mcp/") or key.begins_with("rendering/renderer/") or key.begins_with("audio/driver/"):
		return "editor_restart"
	if key.begins_with("input/"):
		return "none"
	return "project_reload"


func _checked_setting_key(value: Variant, writable: bool) -> Dictionary:
	if not value is String or value.is_empty() or value.length() > 256:
		return _failure("Setting key must be a non-empty string up to 256 characters")
	if value.begins_with("/") or value.ends_with("/") or "//" in value or "\\" in value:
		return _failure("Setting key is invalid")
	for character in value:
		if character.unicode_at(0) < 32:
			return _failure("Setting key contains a control character")
	var lowered: String = value.to_lower()
	var secret_terms := ["password", "secret", "token", "credential", "api_key", "private_key"]
	for term in secret_terms:
		if term in lowered:
			return _failure("Secret-bearing project settings are not exposed")
	if writable and (lowered.begins_with("editor/") or lowered.begins_with("_")):
		return _failure("Editor-only or internal project settings cannot be changed")
	if writable and lowered.begins_with("input/"):
		return _failure("Use input_map_patch for Input Map settings")
	return _success(value)


func _decode_setting_value(value: Variant, target_type: int, depth := 0) -> Dictionary:
	if depth > 6:
		return _failure("Value nesting is too deep")
	if value is String and value.length() > 4096:
		return _failure("String value is too long")
	if target_type == TYPE_NIL:
		if value == null or value is bool or value is int or value is float or value is String:
			return _success(value)
		if value is Array:
			var new_array: Array = []
			if value.size() > 100:
				return _failure("Array has more than 100 entries")
			for child in value:
				var decoded := _decode_setting_value(child, TYPE_NIL, depth + 1)
				if not decoded.ok:
					return decoded
				new_array.append(decoded.result)
			return _success(new_array)
		if value is Dictionary:
			var new_dictionary := {}
			if value.size() > 100:
				return _failure("Dictionary has more than 100 entries")
			for child_key in value:
				if not child_key is String or child_key.length() > 256:
					return _failure("Dictionary keys must be strings up to 256 characters")
				var decoded := _decode_setting_value(value[child_key], TYPE_NIL, depth + 1)
				if not decoded.ok:
					return decoded
				new_dictionary[child_key] = decoded.result
			return _success(new_dictionary)
		return _failure("Unsupported JSON value")
	if target_type == TYPE_INT and (value is int or value is float):
		return _success(int(value))
	if target_type == TYPE_FLOAT and (value is int or value is float):
		return _success(float(value))
	if target_type == TYPE_STRING and value is String:
		return _success(value)
	if target_type == TYPE_STRING_NAME and value is String:
		return _success(StringName(value))
	if target_type == TYPE_NODE_PATH and value is String:
		return _success(NodePath(value))
	if target_type == TYPE_VECTOR2 and _number_array(value, 2):
		return _success(Vector2(float(value[0]), float(value[1])))
	if target_type == TYPE_VECTOR2I and _number_array(value, 2):
		return _success(Vector2i(int(value[0]), int(value[1])))
	if target_type == TYPE_VECTOR3 and _number_array(value, 3):
		return _success(Vector3(float(value[0]), float(value[1]), float(value[2])))
	if target_type == TYPE_VECTOR3I and _number_array(value, 3):
		return _success(Vector3i(int(value[0]), int(value[1]), int(value[2])))
	if target_type == TYPE_COLOR and _number_array(value, 4):
		return _success(Color(float(value[0]), float(value[1]), float(value[2]), float(value[3])))
	if target_type == TYPE_PACKED_STRING_ARRAY and value is Array:
		var strings := PackedStringArray()
		for item in value:
			if not item is String:
				return _failure("Packed string array entries must be strings")
			strings.append(item)
		return _success(strings)
	if target_type in [TYPE_ARRAY, TYPE_DICTIONARY]:
		var decoded := _decode_setting_value(value, TYPE_NIL, depth)
		if decoded.ok and typeof(decoded.result) == target_type:
			return decoded
	if target_type == typeof(value):
		return _success(value)
	return _failure("Value does not match setting type %s" % type_string(target_type))


func _number_array(value: Variant, size: int) -> bool:
	if not value is Array or value.size() != size:
		return false
	for item in value:
		if not item is int and not item is float:
			return false
	return true


func _encode_setting_value(value: Variant, depth := 0) -> Variant:
	if depth > 6:
		return "..."
	if value is InputEvent:
		return _normalize_input_event(value)
	match typeof(value):
		TYPE_NIL, TYPE_BOOL, TYPE_INT, TYPE_FLOAT:
			return value
		TYPE_STRING, TYPE_STRING_NAME, TYPE_NODE_PATH:
			return str(value).left(4096)
		TYPE_VECTOR2, TYPE_VECTOR2I:
			return [value.x, value.y]
		TYPE_VECTOR3, TYPE_VECTOR3I:
			return [value.x, value.y, value.z]
		TYPE_COLOR:
			return [value.r, value.g, value.b, value.a]
		TYPE_ARRAY, TYPE_PACKED_STRING_ARRAY:
			var output: Array = []
			for item in value.slice(0, 100):
				output.append(_encode_setting_value(item, depth + 1))
			return output
		TYPE_DICTIONARY:
			var output := {}
			for key in value.keys().slice(0, 100):
				output[str(key)] = _encode_setting_value(value[key], depth + 1)
			return output
		_:
			return "<unsupported:%s>" % type_string(typeof(value))



