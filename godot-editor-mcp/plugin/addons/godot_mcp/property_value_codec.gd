extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")


func convert(value: Variant, target_type: int) -> Dictionary:
	if target_type == TYPE_INT and (value is int or value is float):
		return ErrorEnvelope.success(int(value))
	if target_type == TYPE_FLOAT and (value is int or value is float):
		return ErrorEnvelope.success(float(value))
	if target_type == TYPE_VECTOR2 and value is Array and value.size() == 2:
		return ErrorEnvelope.success(Vector2(float(value[0]), float(value[1])))
	if target_type == TYPE_VECTOR2I and value is Array and value.size() == 2:
		return ErrorEnvelope.success(Vector2i(int(value[0]), int(value[1])))
	if target_type == TYPE_VECTOR3 and value is Array and value.size() == 3:
		return ErrorEnvelope.success(Vector3(float(value[0]), float(value[1]), float(value[2])))
	if target_type == TYPE_VECTOR3I and value is Array and value.size() == 3:
		return ErrorEnvelope.success(Vector3i(int(value[0]), int(value[1]), int(value[2])))
	if target_type == TYPE_COLOR and value is Array and value.size() in [3, 4]:
		return ErrorEnvelope.success(Color(float(value[0]), float(value[1]), float(value[2]), float(value[3]) if value.size() == 4 else 1.0))
	if target_type == TYPE_NODE_PATH and value is String:
		return ErrorEnvelope.success(NodePath(value))
	if target_type == TYPE_STRING_NAME and value is String:
		return ErrorEnvelope.success(StringName(value))
	if target_type == typeof(value):
		return ErrorEnvelope.success(value)
	return ErrorEnvelope.failure("Value does not match property type %s" % type_string(target_type))


func encode(value: Variant, depth := 0) -> Variant:
	if depth >= 3:
		return "..."
	match typeof(value):
		TYPE_NIL, TYPE_BOOL, TYPE_INT, TYPE_FLOAT:
			return value
		TYPE_STRING, TYPE_STRING_NAME, TYPE_NODE_PATH:
			var text := str(value)
			return text.left(512) + ("..." if text.length() > 512 else "")
		TYPE_VECTOR2, TYPE_VECTOR2I:
			return [value.x, value.y]
		TYPE_VECTOR3, TYPE_VECTOR3I:
			return [value.x, value.y, value.z]
		TYPE_COLOR:
			return [value.r, value.g, value.b, value.a]
		TYPE_ARRAY:
			var array: Array = []
			for item in value.slice(0, 20):
				array.append(encode(item, depth + 1))
			return array
		TYPE_DICTIONARY:
			var dictionary := {}
			for key in value.keys().slice(0, 20):
				dictionary[str(key)] = encode(value[key], depth + 1)
			return dictionary
		TYPE_OBJECT:
			if value == null:
				return null
			if value is Resource and not value.resource_path.is_empty():
				return value.resource_path
			return "<%s>" % value.get_class()
		_:
			return str(value).left(512)
