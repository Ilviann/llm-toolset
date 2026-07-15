extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")


func normalize(event: Variant) -> Dictionary:
	if event is InputEventKey:
		var physical: bool = event.physical_keycode != 0
		var code: int = event.physical_keycode if physical else event.keycode
		return {
			"type": "key", "key": OS.get_keycode_string(code),
			"physical": physical, "device": event.device,
			"shift": event.shift_pressed, "alt": event.alt_pressed,
			"ctrl": event.ctrl_pressed, "meta": event.meta_pressed,
		}
	if event is InputEventMouseButton:
		return {"type": "mouse_button", "button": int(event.button_index), "device": event.device}
	if event is InputEventJoypadButton:
		return {"type": "joypad_button", "button": int(event.button_index), "device": event.device}
	if event is InputEventJoypadMotion:
		return {
			"type": "joypad_motion", "axis": int(event.axis),
			"direction": -1 if event.axis_value < 0 else 1, "device": event.device,
		}
	return {"type": "unsupported", "class": event.get_class() if event is Object else type_string(typeof(event))}


func decode(value: Variant) -> Dictionary:
	if not value is Dictionary:
		return ErrorEnvelope.failure("Input events must be objects")
	if not _only_keys(value, ["type", "key", "physical", "button", "axis", "direction", "device", "shift", "alt", "ctrl", "meta"]):
		return ErrorEnvelope.failure("Input event contains an unsupported field")
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
				return ErrorEnvelope.failure("physical must be a boolean")
			var event := InputEventKey.new()
			event.device = device.result
			if physical:
				event.physical_keycode = code.result
			else:
				event.keycode = code.result
			for modifier in ["shift", "alt", "ctrl", "meta"]:
				if not value.get(modifier, false) is bool:
					return ErrorEnvelope.failure("Key modifiers must be booleans")
			event.shift_pressed = value.get("shift", false)
			event.alt_pressed = value.get("alt", false)
			event.ctrl_pressed = value.get("ctrl", false)
			event.meta_pressed = value.get("meta", false)
			return ErrorEnvelope.success(event)
		"mouse_button":
			var button := _named_index(value.get("button"), {
				"left": 1, "right": 2, "middle": 3, "wheel_up": 4,
				"wheel_down": 5, "wheel_left": 6, "wheel_right": 7,
				"xbutton1": 8, "xbutton2": 9,
			})
			if not button.ok or button.result < 1 or button.result > 9:
				return ErrorEnvelope.failure("Mouse button is invalid")
			var event := InputEventMouseButton.new()
			event.device = device.result
			event.button_index = button.result
			return ErrorEnvelope.success(event)
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
				return ErrorEnvelope.failure("Joypad button is invalid")
			var event := InputEventJoypadButton.new()
			event.device = device.result
			event.button_index = button.result
			return ErrorEnvelope.success(event)
		"joypad_motion":
			var axis := _named_index(value.get("axis"), {
				"left_x": 0, "left_y": 1, "right_x": 2, "right_y": 3,
				"trigger_left": 4, "trigger_right": 5,
			})
			var direction = value.get("direction")
			if not axis.ok or axis.result < 0 or axis.result > 5:
				return ErrorEnvelope.failure("Joypad axis is invalid")
			if not (direction is int or direction is float) or float(direction) not in [-1.0, 1.0]:
				return ErrorEnvelope.failure("Joypad motion direction must be -1 or 1")
			var event := InputEventJoypadMotion.new()
			event.device = device.result
			event.axis = axis.result
			event.axis_value = float(direction)
			return ErrorEnvelope.success(event)
		_:
			return ErrorEnvelope.failure("Input event type must be key, mouse_button, joypad_button, or joypad_motion")


func _keycode(value: Variant) -> Dictionary:
	if (value is int or value is float) and float(value) == floor(float(value)) and value > 0:
		return ErrorEnvelope.success(int(value))
	if value is String and not value.is_empty() and value.length() <= 64:
		var code := OS.find_keycode_from_string(value)
		if code != 0:
			return ErrorEnvelope.success(code)
	return ErrorEnvelope.failure("Key must be a recognized name or positive Godot keycode")


func _bounded_device(value: Variant) -> Dictionary:
	if not (value is int or value is float) or float(value) != floor(float(value)) or value < -1 or value > 32:
		return ErrorEnvelope.failure("Input device must be an integer between -1 and 32")
	return ErrorEnvelope.success(int(value))


func _named_index(value: Variant, names: Dictionary) -> Dictionary:
	if (value is int or value is float) and float(value) == floor(float(value)):
		return ErrorEnvelope.success(int(value))
	if value is String and value.to_lower() in names:
		return ErrorEnvelope.success(names[value.to_lower()])
	return ErrorEnvelope.failure("Named index is invalid")


func _only_keys(dictionary: Dictionary, allowed: Array) -> bool:
	for key in dictionary:
		if key not in allowed:
			return false
	return true
