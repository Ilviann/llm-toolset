extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")
const Limits := preload("command_limits.gd")
const MAX_INPUT_EVENTS := Limits.MAX_INPUT_EVENTS

var _mark_project_file_saved: Callable
var _input_events: RefCounted


func _init(mark_project_file_saved: Callable, input_events: RefCounted) -> void:
	_mark_project_file_saved = mark_project_file_saved
	_input_events = input_events


func handlers() -> Dictionary:
	return {"input_map_patch": Callable(self, "_input_map_patch")}


func _input_map_patch(arguments: Dictionary) -> Dictionary:
	if not _only_keys(arguments, ["action", "deadzone", "add_events", "remove_events", "save", "dry_run"]):
		return _failure("input_map_patch contains an unsupported field")
	var action = arguments.get("action")
	if not action is String or action.is_empty() or action.length() > 128 or "/" in action:
		return _failure("Action must be a non-empty name up to 128 characters")
	var add_events = arguments.get("add_events", [])
	var remove_events = arguments.get("remove_events", [])
	var save = arguments.get("save", true)
	var dry_run = arguments.get("dry_run", false)
	if not add_events is Array or add_events.size() > MAX_INPUT_EVENTS:
		return _failure("add_events must be an array with at most 32 entries")
	if not remove_events is Array or remove_events.size() > MAX_INPUT_EVENTS:
		return _failure("remove_events must be an array with at most 32 entries")
	if not save is bool or not dry_run is bool:
		return _failure("save and dry_run must be booleans")
	var deadzone_value = arguments.get("deadzone", null)
	if deadzone_value != null and (not (deadzone_value is int or deadzone_value is float) or float(deadzone_value) < 0.0 or float(deadzone_value) > 1.0):
		return _failure("deadzone must be between 0 and 1")
	var added: Array[InputEvent] = []
	var removed: Array[Dictionary] = []
	for raw_event in add_events:
		var converted: Dictionary = _input_events.decode(raw_event)
		if not converted.ok:
			return converted
		added.append(converted.result)
	for raw_event in remove_events:
		var converted: Dictionary = _input_events.decode(raw_event)
		if not converted.ok:
			return converted
		removed.append(_input_events.normalize(converted.result))
	var setting_key := "input/%s" % action
	var existed := ProjectSettings.has_setting(setting_key)
	var before_setting = ProjectSettings.get_setting(setting_key) if existed else {"deadzone": 0.5, "events": []}
	if not before_setting is Dictionary or not before_setting.get("events", []) is Array:
		return _failure("Existing Input Map action has an unsupported format")
	var after_setting: Dictionary = before_setting.duplicate(true)
	var after_events: Array = after_setting.get("events", []).duplicate()
	var removed_count := 0
	for index in range(after_events.size() - 1, -1, -1):
		var normalized: Dictionary = _input_events.normalize(after_events[index])
		if normalized in removed:
			after_events.remove_at(index)
			removed_count += 1
	var added_count := 0
	for event in added:
		var normalized: Dictionary = _input_events.normalize(event)
		var duplicate := false
		for existing in after_events:
			if _input_events.normalize(existing) == normalized:
				duplicate = true
				break
		if not duplicate:
			after_events.append(event)
			added_count += 1
	after_setting["events"] = after_events
	if deadzone_value != null:
		after_setting["deadzone"] = float(deadzone_value)
	elif not after_setting.has("deadzone"):
		after_setting["deadzone"] = 0.5
	var before := _normalized_input_action(action, before_setting, existed)
	var after := _normalized_input_action(action, after_setting, true)
	if not dry_run:
		ProjectSettings.set_setting(setting_key, after_setting)
		InputMap.load_from_project_settings()
		if save:
			var error := ProjectSettings.save()
			if error != OK:
				if existed:
					ProjectSettings.set_setting(setting_key, before_setting)
				else:
					ProjectSettings.clear(setting_key)
				InputMap.load_from_project_settings()
				ProjectSettings.save()
				return _failure("Could not save Input Map (Godot error %d); transaction rolled back" % error)
			if _mark_project_file_saved.is_valid():
				_mark_project_file_saved.call(false)
	return _success({
		"diff": {"before": before, "after": after, "changed": before != after},
		"added": added_count,
		"removed": removed_count,
		"dry_run": dry_run,
		"saved": save and not dry_run,
		"requirements": {
			"editor_refresh": before != after,
			"project_reload": false,
			"editor_restart": false,
		},
	})


func _normalized_input_action(action: String, setting: Dictionary, exists: bool) -> Dictionary:
	var events: Array = []
	for event in setting.get("events", []):
		events.append(_input_events.normalize(event))
	return {
		"action": action,
		"exists": exists,
		"deadzone": float(setting.get("deadzone", 0.5)),
		"events": events,
	}


func _only_keys(dictionary: Dictionary, allowed: Array) -> bool:
	for key in dictionary:
		if key not in allowed:
			return false
	return true


func _success(result: Variant) -> Dictionary:
	return ErrorEnvelope.success(result)


func _failure(message: String) -> Dictionary:
	return ErrorEnvelope.failure(message)
