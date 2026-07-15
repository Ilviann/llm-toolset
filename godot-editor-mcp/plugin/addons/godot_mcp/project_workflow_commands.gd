extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")
const Limits := preload("command_limits.gd")

const RUNTIME_PROBE_NAME := "GodotMCPRuntimeProbe"
const RUNTIME_PROBE_PATH := "res://addons/godot_mcp/runtime_probe.gd"
const AUTOLOAD_PREFIX := "autoload/"
const ENABLED_PLUGINS_SETTING := "editor_plugins/enabled"

var _add_autoload: Callable
var _remove_autoload: Callable
var _mark_project_file_saved: Callable
var _project_paths: RefCounted
var _save_settings: Callable


func _init(
	add_autoload: Callable,
	remove_autoload: Callable,
	mark_project_file_saved: Callable,
	project_paths: RefCounted,
	save_settings := Callable(),
) -> void:
	_add_autoload = add_autoload
	_remove_autoload = remove_autoload
	_mark_project_file_saved = mark_project_file_saved
	_project_paths = project_paths
	_save_settings = save_settings


func handlers() -> Dictionary:
	return {
		"list_autoloads": Callable(self, "_list_autoloads"),
		"autoload_patch": Callable(self, "_autoload_patch"),
		"list_editor_plugins": Callable(self, "_list_editor_plugins"),
	}


func _list_autoloads(arguments: Dictionary) -> Dictionary:
	if not arguments.is_empty():
		return _failure("list_autoloads does not accept arguments")
	var state := _autoload_state()
	var names: Array = state.keys()
	names.sort()
	var records: Array[Dictionary] = []
	for name in names.slice(0, Limits.MAX_AUTOLOADS):
		records.append(_public_entry(str(name), state[name]))
	return _success({
		"autoloads": records,
		"count": records.size(),
		"truncated": names.size() > records.size(),
	})


func _autoload_patch(arguments: Dictionary) -> Dictionary:
	if not _only_keys(arguments, ["changes", "save", "dry_run"]):
		return _failure("autoload_patch contains an unsupported field")
	var changes = arguments.get("changes")
	if (
		not changes is Array
		or changes.is_empty()
		or changes.size() > Limits.MAX_AUTOLOAD_CHANGES
	):
		return _failure("changes must contain between 1 and 32 entries")
	var save = arguments.get("save", true)
	var dry_run = arguments.get("dry_run", false)
	if not save is bool or not dry_run is bool:
		return _failure("save and dry_run must be booleans")

	var original := _autoload_state()
	var planned: Dictionary = original.duplicate(true)
	var seen := {}
	var prepared: Array[Dictionary] = []
	for value in changes:
		if not value is Dictionary:
			return _failure("Each autoload change must be an object")
		var change := value as Dictionary
		if not _only_keys(change, ["op", "name", "path", "expected"]):
			return _failure("An autoload change contains an unsupported field")
		var operation = change.get("op")
		if operation not in ["add", "update", "remove"]:
			return _failure("Autoload op must be add, update, or remove")
		var checked_name := _checked_name(change.get("name"))
		if not checked_name.ok:
			return checked_name
		var name := checked_name.result as String
		if name == RUNTIME_PROBE_NAME:
			return ErrorEnvelope.failure(
				"The Godot MCP runtime probe autoload is protected",
				ErrorEnvelope.PROTECTED_PATH,
				{"name": name}, false,
			)
		if name in seen:
			return _failure("Duplicate autoload name in changes: %s" % name)
		seen[name] = true
		var before: Variant = planned.get(name)
		if change.has("expected"):
			var expected := _checked_expected(change.expected)
			if not expected.ok:
				return expected
			if not _expected_matches(expected.result, before):
				return ErrorEnvelope.failure(
					"Autoload compare-and-swap failed for %s" % name,
					ErrorEnvelope.INVALID_ARGUMENT,
					{
						"name": name,
						"expected": expected.result,
						"actual": _public_entry(name, before) if before != null else null,
					}, false,
				)

		var after: Variant = null
		match operation:
			"add":
				if before != null:
					return _failure("Autoload already exists: %s" % name)
				var conflict := _name_conflict(name)
				if not conflict.is_empty():
					return _failure(conflict)
				after = _checked_new_entry(change, name)
				if after is Dictionary and not after.get("ok", true):
					return after
			"update":
				if before == null:
					return ErrorEnvelope.failure(
						"Autoload does not exist: %s" % name,
						ErrorEnvelope.NOT_FOUND,
					)
				after = _checked_new_entry(change, name)
				if after is Dictionary and not after.get("ok", true):
					return after
			"remove":
				if change.has("path"):
					return _failure("remove autoload changes cannot include path")
				if before == null:
					return ErrorEnvelope.failure(
						"Autoload does not exist: %s" % name,
						ErrorEnvelope.NOT_FOUND,
					)

		if after is Dictionary and after.has("result"):
			after = after.result
		if after == null:
			planned.erase(name)
		else:
			planned[name] = after
		prepared.append({
			"op": operation,
			"name": name,
			"before": before,
			"after": after,
			"changed": before != after,
		})

	var changed := prepared.any(func(item: Dictionary) -> bool: return item.changed)
	if not dry_run and changed:
		for item in prepared:
			if not item.changed:
				continue
			_apply_entry(item.name, item.after)
			if not _entry_matches(item.name, item.after):
				_restore_entries(original, prepared)
				return _failure("Godot rejected autoload change for %s" % item.name)
		if save:
			var save_error := _save_project_settings()
			if save_error != OK:
				_restore_entries(original, prepared)
				_save_project_settings()
				return ErrorEnvelope.failure(
					"Could not save autoload changes; transaction rolled back",
					ErrorEnvelope.SAVE_FAILED,
					{"error": save_error}, true,
				)
			if _mark_project_file_saved.is_valid():
				_mark_project_file_saved.call(true)

	var diff: Array[Dictionary] = []
	for item in prepared:
		diff.append({
			"op": item.op,
			"name": item.name,
			"before": _public_entry(item.name, item.before) if item.before != null else null,
			"after": _public_entry(item.name, item.after) if item.after != null else null,
			"changed": item.changed,
		})
	return _success({
		"diff": diff,
		"dry_run": dry_run,
		"saved": save and not dry_run and changed,
		"requirements": {
			"editor_refresh": false,
			"project_reload": changed,
			"editor_restart": false,
		},
	})


func _checked_new_entry(change: Dictionary, name: String) -> Dictionary:
	if not change.has("path"):
		return _failure("%s autoload changes must include path" % change.op)
	var checked := _checked_path(change.path, true)
	if not checked.ok:
		return checked
	var path := checked.result as String
	if path == RUNTIME_PROBE_PATH:
		return ErrorEnvelope.failure(
			"The Godot MCP runtime probe script is protected",
			ErrorEnvelope.PROTECTED_PATH,
			{"name": name, "path": path}, false,
		)
	return _success({"path": path, "singleton": true})


func _checked_expected(value: Variant) -> Dictionary:
	if value == null:
		return _success(null)
	if value is String:
		var checked := _checked_path(value, false)
		if not checked.ok:
			return checked
		return _success({"path": checked.result, "singleton": true})
	if value is Dictionary:
		if not _only_keys(value, ["path", "singleton"]) or not value.has("path"):
			return _failure("expected autoload values require path and optional singleton")
		var singleton = value.get("singleton", true)
		if not singleton is bool:
			return _failure("expected singleton must be a boolean")
		var checked := _checked_path(value.path, false)
		if not checked.ok:
			return checked
		return _success({"path": checked.result, "singleton": singleton})
	return _failure("expected must be null, a res:// path, or an autoload value")


func _expected_matches(expected: Variant, actual: Variant) -> bool:
	if expected == null:
		return actual == null
	return actual != null and expected == actual


func _checked_name(value: Variant) -> Dictionary:
	if (
		not value is String
		or value.is_empty()
		or value.length() > 128
		or not value.is_valid_identifier()
	):
		return _failure("Autoload name must be a valid identifier up to 128 characters")
	return _success(value)


func _name_conflict(name: String) -> String:
	if ClassDB.class_exists(name):
		return "Autoload name conflicts with a built-in class: %s" % name
	if Engine.has_singleton(name):
		return "Autoload name conflicts with an engine singleton: %s" % name
	for info in ProjectSettings.get_global_class_list():
		if str(info.get("class", "")) == name:
			return "Autoload name conflicts with a global script class: %s" % name
	return ""


func _checked_path(value: Variant, require_exists: bool) -> Dictionary:
	if not value is String or not value.begins_with("res://"):
		return _failure("Autoload path must be a res:// script or scene path")
	var relative: String = value.trim_prefix("res://")
	var checked: Dictionary = _project_paths.check(
		relative, false, PackedStringArray(["gd", "cs", "tscn", "scn"]),
	)
	if not checked.ok:
		return checked
	var normalized := checked.result as String
	if require_exists and not FileAccess.file_exists(normalized):
		return ErrorEnvelope.failure(
			"Autoload script or scene does not exist: %s" % normalized,
			ErrorEnvelope.NOT_FOUND,
		)
	if require_exists:
		var resource = ResourceLoader.load(normalized)
		if resource is Script:
			var script := resource as Script
			if not ClassDB.is_parent_class(script.get_instance_base_type(), "Node"):
				return _failure("Autoload scripts must be valid and inherit Node")
		elif not resource is PackedScene:
			return _failure("Autoload path must contain a script or PackedScene")
	return _success(normalized)


func _autoload_state() -> Dictionary:
	var output := {}
	for info in ProjectSettings.get_property_list():
		var key := str(info.get("name", ""))
		if not key.begins_with(AUTOLOAD_PREFIX):
			continue
		var name := key.trim_prefix(AUTOLOAD_PREFIX)
		var entry := _entry_from_raw(ProjectSettings.get_setting(key, ""))
		if not name.is_empty() and entry != null:
			output[name] = entry
	return output


func _entry_from_raw(value: Variant) -> Variant:
	if not value is String or value.is_empty():
		return null
	var raw := value as String
	var singleton := raw.begins_with("*")
	var path := raw.trim_prefix("*")
	if path.begins_with("uid://"):
		var uid := ResourceUID.text_to_id(path)
		if uid != ResourceUID.INVALID_ID:
			var resolved := ResourceUID.get_id_path(uid)
			if not resolved.is_empty():
				path = resolved
	return {"path": path, "singleton": singleton}


func _public_entry(name: String, entry: Dictionary) -> Dictionary:
	var value := {
		"path": str(entry.path),
		"singleton": bool(entry.singleton),
	}
	return {
		"name": name,
		"path": value.path,
		"singleton": value.singleton,
		"value": value,
		"protected": name == RUNTIME_PROBE_NAME or str(entry.path) == RUNTIME_PROBE_PATH,
	}


func _apply_entry(name: String, entry: Variant) -> void:
	var key := AUTOLOAD_PREFIX + name
	if ProjectSettings.has_setting(key):
		_remove_autoload.call(name)
	if entry != null:
		_add_autoload.call(name, str(entry.path))


func _entry_matches(name: String, expected: Variant) -> bool:
	var key := AUTOLOAD_PREFIX + name
	if expected == null:
		return not ProjectSettings.has_setting(key)
	if not ProjectSettings.has_setting(key):
		return false
	return _entry_from_raw(ProjectSettings.get_setting(key)) == expected


func _restore_entries(original: Dictionary, prepared: Array[Dictionary]) -> void:
	for item in prepared:
		var name := item.name as String
		var key := AUTOLOAD_PREFIX + name
		if ProjectSettings.has_setting(key):
			_remove_autoload.call(name)
		if original.has(name):
			var entry: Dictionary = original[name]
			_add_autoload.call(name, str(entry.path))
			if not entry.singleton:
				ProjectSettings.set_setting(key, str(entry.path))


func _save_project_settings() -> int:
	if _save_settings.is_valid():
		return int(_save_settings.call())
	return ProjectSettings.save()


func _list_editor_plugins(arguments: Dictionary) -> Dictionary:
	if not arguments.is_empty():
		return _failure("list_editor_plugins does not accept arguments")
	var enabled_value = ProjectSettings.get_setting(ENABLED_PLUGINS_SETTING, PackedStringArray())
	var enabled := {}
	var enabled_valid := enabled_value is PackedStringArray or enabled_value is Array
	if enabled_valid:
		for value in enabled_value:
			if value is String and value.begins_with("res://"):
				enabled[value] = true
	var paths := enabled.duplicate()
	var addons := DirAccess.open("res://addons")
	if addons != null:
		for directory in addons.get_directories():
			var path := "res://addons/%s/plugin.cfg" % directory
			if FileAccess.file_exists(path):
				paths[path] = true
	var sorted_paths: Array = paths.keys()
	sorted_paths.sort()
	var plugins: Array[Dictionary] = []
	for path_value in sorted_paths.slice(0, Limits.MAX_EDITOR_PLUGINS):
		var path := str(path_value)
		var installed := FileAccess.file_exists(path)
		var config := ConfigFile.new()
		var load_error := config.load(path) if installed else ERR_FILE_NOT_FOUND
		var valid := load_error == OK and config.has_section("plugin")
		var record := {
			"path": path.left(512),
			"enabled": enabled.has(path),
			"installed": installed,
			"valid": valid,
		}
		if valid:
			var script := str(config.get_value("plugin", "script", "")).left(256)
			record["name"] = str(config.get_value("plugin", "name", "")).left(256)
			record["author"] = str(config.get_value("plugin", "author", "")).left(256)
			record["version"] = str(config.get_value("plugin", "version", "")).left(64)
			record["script"] = script
			record["language"] = script.get_extension().to_lower()
		else:
			record["error"] = load_error
		plugins.append(record)
	return _success({
		"plugins": plugins,
		"count": plugins.size(),
		"truncated": sorted_paths.size() > plugins.size(),
		"enabled_setting_valid": enabled_valid,
	})


func _only_keys(dictionary: Dictionary, allowed: Array) -> bool:
	for key in dictionary:
		if key not in allowed:
			return false
	return true


func _success(result: Variant) -> Dictionary:
	return ErrorEnvelope.success(result)


func _failure(message: String) -> Dictionary:
	return ErrorEnvelope.failure(message, ErrorEnvelope.INVALID_ARGUMENT)
