extends "command_base.gd"

const Limits := preload("command_limits.gd")
const MAX_INPUT_EVENTS := Limits.MAX_INPUT_EVENTS


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
		var converted := _input_event_from_json(raw_event)
		if not converted.ok:
			return converted
		added.append(converted.result)
	for raw_event in remove_events:
		var converted := _input_event_from_json(raw_event)
		if not converted.ok:
			return converted
		removed.append(_normalize_input_event(converted.result))
	var setting_key := "input/%s" % action
	var existed := ProjectSettings.has_setting(setting_key)
	var before_setting = ProjectSettings.get_setting(setting_key) if existed else {"deadzone": 0.5, "events": []}
	if not before_setting is Dictionary or not before_setting.get("events", []) is Array:
		return _failure("Existing Input Map action has an unsupported format")
	var after_setting: Dictionary = before_setting.duplicate(true)
	var after_events: Array = after_setting.get("events", []).duplicate()
	var removed_count := 0
	for index in range(after_events.size() - 1, -1, -1):
		var normalized := _normalize_input_event(after_events[index])
		if normalized in removed:
			after_events.remove_at(index)
			removed_count += 1
	var added_count := 0
	for event in added:
		var normalized := _normalize_input_event(event)
		var duplicate := false
		for existing in after_events:
			if _normalize_input_event(existing) == normalized:
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
			if _state_monitor != null:
				_state_monitor.mark_project_settings_saved(false)
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
		events.append(_normalize_input_event(event))
	return {
		"action": action,
		"exists": exists,
		"deadzone": float(setting.get("deadzone", 0.5)),
		"events": events,
	}


func _input_event_from_json(value: Variant) -> Dictionary:
	if not value is Dictionary:
		return _failure("Input events must be objects")
	if not _only_keys(value, ["type", "key", "physical", "button", "axis", "direction", "device", "shift", "alt", "ctrl", "meta"]):
		return _failure("Input event contains an unsupported field")
	var event_type = value.get("type")
	var device := _bounded_device(value.get("device", -1))
	if not device.ok:
		return device
	match event_type:
		"key":
			var code := _keycode(value.get("key"))
			if not code.ok:
				return code
			var physical = value.get("physical", false)
			if not physical is bool:
				return _failure("physical must be a boolean")
			var event := InputEventKey.new()
			event.device = device.result
			if physical:
				event.physical_keycode = code.result
			else:
				event.keycode = code.result
			for modifier in ["shift", "alt", "ctrl", "meta"]:
				if not value.get(modifier, false) is bool:
					return _failure("Key modifiers must be booleans")
			event.shift_pressed = value.get("shift", false)
			event.alt_pressed = value.get("alt", false)
			event.ctrl_pressed = value.get("ctrl", false)
			event.meta_pressed = value.get("meta", false)
			return _success(event)
		"mouse_button":
			var button := _named_index(value.get("button"), {
				"left": 1, "right": 2, "middle": 3, "wheel_up": 4,
				"wheel_down": 5, "wheel_left": 6, "wheel_right": 7,
				"xbutton1": 8, "xbutton2": 9,
			})
			if not button.ok or button.result < 1 or button.result > 9:
				return _failure("Mouse button is invalid")
			var event := InputEventMouseButton.new()
			event.device = device.result
			event.button_index = button.result
			return _success(event)
		"joypad_button":
			var button := _named_index(value.get("button"), {
				"a": 0, "b": 1, "x": 2, "y": 3, "back": 4, "guide": 5,
				"start": 6, "left_stick": 7, "right_stick": 8,
				"left_shoulder": 9, "right_shoulder": 10, "dpad_up": 11,
				"dpad_down": 12, "dpad_left": 13, "dpad_right": 14,
				"misc1": 15, "paddle1": 16, "paddle2": 17, "paddle3": 18,
				"paddle4": 19, "touchpad": 20,
			})
			if not button.ok or button.result < 0 or button.result > 20:
				return _failure("Joypad button is invalid")
			var event := InputEventJoypadButton.new()
			event.device = device.result
			event.button_index = button.result
			return _success(event)
		"joypad_motion":
			var axis := _named_index(value.get("axis"), {
				"left_x": 0, "left_y": 1, "right_x": 2, "right_y": 3,
				"trigger_left": 4, "trigger_right": 5,
			})
			var direction = value.get("direction")
			if not axis.ok or axis.result < 0 or axis.result > 5:
				return _failure("Joypad axis is invalid")
			if not (direction is int or direction is float) or float(direction) not in [-1.0, 1.0]:
				return _failure("Joypad motion direction must be -1 or 1")
			var event := InputEventJoypadMotion.new()
			event.device = device.result
			event.axis = axis.result
			event.axis_value = float(direction)
			return _success(event)
		_:
			return _failure("Input event type must be key, mouse_button, joypad_button, or joypad_motion")


func _keycode(value: Variant) -> Dictionary:
	if (value is int or value is float) and float(value) == floor(float(value)) and value > 0:
		return _success(int(value))
	if value is String and not value.is_empty() and value.length() <= 64:
		var code := OS.find_keycode_from_string(value)
		if code != 0:
			return _success(code)
	return _failure("Key must be a recognized name or positive Godot keycode")


func _bounded_device(value: Variant) -> Dictionary:
	if not (value is int or value is float) or float(value) != floor(float(value)) or value < -1 or value > 32:
		return _failure("Input device must be an integer between -1 and 32")
	return _success(int(value))


func _named_index(value: Variant, names: Dictionary) -> Dictionary:
	if (value is int or value is float) and float(value) == floor(float(value)):
		return _success(int(value))
	if value is String and value.to_lower() in names:
		return _success(names[value.to_lower()])
	return _failure("Named index is invalid")
