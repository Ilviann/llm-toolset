extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")
const Limits := preload("command_limits.gd")
const MAX_SETTINGS := Limits.MAX_SETTINGS
const MAX_SETTING_CHANGES := Limits.MAX_SETTING_CHANGES

var _mark_project_file_saved: Callable
var _input_events: RefCounted
var _property_values: RefCounted


func _init(
	mark_project_file_saved: Callable,
	input_events: RefCounted,
	property_values: RefCounted,
) -> void:
	_mark_project_file_saved = mark_project_file_saved
	_input_events = input_events
	_property_values = property_values


func handlers() -> Dictionary:
	return {
		"project_settings_get": Callable(self, "_project_settings_get"),
		"project_settings_patch": Callable(self, "_project_settings_patch"),
	}


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
					return _failure("Invalid expected value for %s: %s" % [key, ErrorEnvelope.message(expected)])
				if expected.result != before:
					return _failure("Compare-and-swap failed for %s" % key)
		var converted := _decode_setting_value(change.value, typeof(before) if existed else TYPE_NIL)
		if not converted.ok:
			return _failure("Invalid value for %s: %s" % [key, ErrorEnvelope.message(converted)])
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
		if save and _mark_project_file_saved.is_valid():
			var requirements := _combined_reload_requirements(prepared)
			_mark_project_file_saved.call(requirements.project_reload or requirements.editor_restart)
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


func _decode_setting_value(value: Variant, target_type: int) -> Dictionary:
	return _property_values.convert(value, target_type)


func _encode_setting_value(value: Variant) -> Variant:
	if value is InputEvent:
		return _input_events.normalize(value)
	return _property_values.encode(value)


func _only_keys(dictionary: Dictionary, allowed: Array) -> bool:
	for key in dictionary:
		if key not in allowed:
			return false
	return true


func _success(result: Variant) -> Dictionary:
	return ErrorEnvelope.success(result)


func _failure(message: String) -> Dictionary:
	return ErrorEnvelope.failure(message)
