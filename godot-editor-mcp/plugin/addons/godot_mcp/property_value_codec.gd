extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")
const Limits := preload("command_limits.gd")

const TAG := "$type"


func supported_forms() -> Array[String]:
	return [
		"null", "bool", "int", "float", "string", "string_name",
		"node_path", "node", "resource", "vector2", "vector2i", "vector3",
		"vector3i", "vector4", "vector4i", "color", "rect2", "rect2i",
		"transform2d", "plane", "quaternion", "aabb", "basis", "transform3d",
		"enum", "flags", "array", "dictionary", "packed_byte_array",
		"packed_int32_array", "packed_int64_array", "packed_float32_array",
		"packed_float64_array", "packed_string_array", "packed_vector2_array",
		"packed_vector3_array", "packed_color_array",
	]


func convert(
	value: Variant,
	target_type: int,
	property_info := {},
	scene_root: Node = null,
	node_resolver := Callable(),
) -> Dictionary:
	var bounded := _validate_json_value(value)
	if not bounded.ok:
		return bounded
	var converted := _decode(value, target_type, property_info, scene_root, node_resolver)
	if not converted.ok:
		return converted
	return _validate_property_compatibility(converted.result, target_type, property_info)


func encode(value: Variant, property_info := {}, scene_root: Node = null) -> Variant:
	var encoded := _encode_value(value, property_info, scene_root, 0)
	if JSON.stringify(encoded).to_utf8_buffer().size() > Limits.MAX_VALUE_BYTES:
		return {TAG: "truncated", "reason": "encoded value exceeds limit"}
	return encoded


func _decode(
	value: Variant,
	target_type: int,
	property_info: Dictionary,
	scene_root: Node,
	node_resolver: Callable,
) -> Dictionary:
	if value == null:
		if target_type in [TYPE_NIL, TYPE_OBJECT]:
			return ErrorEnvelope.success(null)
		return _failure("Null does not match this property type")
	if target_type == TYPE_NIL:
		return ErrorEnvelope.success(value.duplicate(true) if value is Array or value is Dictionary else value)
	if value is Dictionary and value.has(TAG):
		return _decode_tagged(value, target_type, property_info, scene_root, node_resolver)
	match target_type:
		TYPE_BOOL:
			if value is bool:
				return ErrorEnvelope.success(value)
		TYPE_INT:
			if value is int:
				return ErrorEnvelope.success(value)
			if value is float and is_finite(value) and value == floor(value):
				return ErrorEnvelope.success(int(value))
		TYPE_FLOAT:
			if (value is int or value is float) and not value is bool and is_finite(float(value)):
				return ErrorEnvelope.success(float(value))
		TYPE_STRING:
			if value is String:
				return ErrorEnvelope.success(value)
		TYPE_STRING_NAME:
			if value is String:
				return ErrorEnvelope.success(StringName(value))
		TYPE_NODE_PATH:
			if value is String:
				return _decode_node_path(value, scene_root)
		TYPE_VECTOR2:
			return _vector(value, 2, false, "Vector2", Callable(self, "_make_vector2"))
		TYPE_VECTOR2I:
			return _vector(value, 2, true, "Vector2i", Callable(self, "_make_vector2i"))
		TYPE_VECTOR3:
			return _vector(value, 3, false, "Vector3", Callable(self, "_make_vector3"))
		TYPE_VECTOR3I:
			return _vector(value, 3, true, "Vector3i", Callable(self, "_make_vector3i"))
		TYPE_VECTOR4:
			return _vector(value, 4, false, "Vector4", Callable(self, "_make_vector4"))
		TYPE_VECTOR4I:
			return _vector(value, 4, true, "Vector4i", Callable(self, "_make_vector4i"))
		TYPE_COLOR:
			if value is Array and value.size() in [3, 4]:
				var components := _numbers(value, false, "Color")
				if components.ok:
					var values: Array = components.result
					return ErrorEnvelope.success(Color(
						values[0], values[1], values[2], values[3] if values.size() == 4 else 1.0
					))
		TYPE_ARRAY:
			if value is Array:
				return ErrorEnvelope.success(value.duplicate(true))
		TYPE_DICTIONARY:
			if value is Dictionary:
				return ErrorEnvelope.success(value.duplicate(true))
		TYPE_PACKED_STRING_ARRAY:
			return _packed_array(value, "packed_string_array")
		TYPE_PACKED_BYTE_ARRAY:
			return _packed_array(value, "packed_byte_array")
		TYPE_PACKED_INT32_ARRAY:
			return _packed_array(value, "packed_int32_array")
		TYPE_PACKED_INT64_ARRAY:
			return _packed_array(value, "packed_int64_array")
		TYPE_PACKED_FLOAT32_ARRAY:
			return _packed_array(value, "packed_float32_array")
		TYPE_PACKED_FLOAT64_ARRAY:
			return _packed_array(value, "packed_float64_array")
		TYPE_PACKED_VECTOR2_ARRAY:
			return _packed_array(value, "packed_vector2_array")
		TYPE_PACKED_VECTOR3_ARRAY:
			return _packed_array(value, "packed_vector3_array")
		TYPE_PACKED_COLOR_ARRAY:
			return _packed_array(value, "packed_color_array")
	if target_type == typeof(value):
		return ErrorEnvelope.success(value)
	return _failure("Value does not match property type %s" % type_string(target_type))


func _decode_tagged(
	value: Dictionary,
	target_type: int,
	property_info: Dictionary,
	scene_root: Node,
	node_resolver: Callable,
) -> Dictionary:
	var kind = value.get(TAG)
	if not kind is String or kind.is_empty() or kind.length() > 64:
		return _failure("Tagged value type is invalid")
	match kind:
		"string_name":
			return _tagged_string(value, "value", TYPE_STRING_NAME, target_type)
		"node_path":
			if target_type != TYPE_NODE_PATH:
				return _type_mismatch(kind, target_type)
			return _decode_node_path(value.get("path"), scene_root)
		"node":
			if target_type != TYPE_OBJECT:
				return _type_mismatch(kind, target_type)
			return _decode_node_reference(value, scene_root, node_resolver)
		"resource":
			if target_type != TYPE_OBJECT:
				return _type_mismatch(kind, target_type)
			return _decode_resource_reference(value, property_info)
		"enum":
			if target_type != TYPE_INT:
				return _type_mismatch(kind, target_type)
			return _decode_enum(value.get("value"), property_info, false)
		"flags":
			if target_type != TYPE_INT:
				return _type_mismatch(kind, target_type)
			return _decode_enum(value.get("value"), property_info, true)
		"rect2":
			if target_type != TYPE_RECT2:
				return _type_mismatch(kind, target_type)
			return _decode_rect(value, false)
		"rect2i":
			if target_type != TYPE_RECT2I:
				return _type_mismatch(kind, target_type)
			return _decode_rect(value, true)
		"transform2d":
			if target_type != TYPE_TRANSFORM2D:
				return _type_mismatch(kind, target_type)
			return _decode_transform2d(value)
		"plane":
			if target_type != TYPE_PLANE:
				return _type_mismatch(kind, target_type)
			var plane := _numbers(value.get("value"), false, "Plane", 4)
			return plane if not plane.ok else ErrorEnvelope.success(Plane(plane.result[0], plane.result[1], plane.result[2], plane.result[3]))
		"quaternion":
			if target_type != TYPE_QUATERNION:
				return _type_mismatch(kind, target_type)
			var quaternion := _numbers(value.get("value"), false, "Quaternion", 4)
			return quaternion if not quaternion.ok else ErrorEnvelope.success(Quaternion(quaternion.result[0], quaternion.result[1], quaternion.result[2], quaternion.result[3]))
		"aabb":
			if target_type != TYPE_AABB:
				return _type_mismatch(kind, target_type)
			return _decode_aabb(value)
		"basis":
			if target_type != TYPE_BASIS:
				return _type_mismatch(kind, target_type)
			return _decode_basis(value.get("value"))
		"transform3d":
			if target_type != TYPE_TRANSFORM3D:
				return _type_mismatch(kind, target_type)
			return _decode_transform3d(value)
		"packed_byte_array", "packed_int32_array", "packed_int64_array", \
		"packed_float32_array", "packed_float64_array", "packed_string_array", \
		"packed_vector2_array", "packed_vector3_array", "packed_color_array":
			return _packed_array(value.get("values"), kind)
	return _failure("Unsupported tagged value type: %s" % kind)


func _decode_node_path(path_value: Variant, scene_root: Node) -> Dictionary:
	if not path_value is String or path_value.is_empty() or path_value.length() > 512:
		return _failure("NodePath must be a non-empty string up to 512 characters")
	if path_value.begins_with("/") or ".." in path_value.split("/"):
		return _failure("NodePath must be scene-relative and cannot contain ..")
	return ErrorEnvelope.success(NodePath(path_value))


func _decode_node_reference(value: Dictionary, scene_root: Node, resolver: Callable) -> Dictionary:
	var reference = {"path": value.get("path")} if value.has("path") else {"handle": value.get("handle")}
	if resolver.is_valid():
		var resolved = resolver.call(reference)
		if resolved is Dictionary:
			return resolved
		return _failure("Node resolver returned an invalid result")
	if scene_root == null:
		return _failure("Node references require an edited scene")
	if not reference.has("path"):
		return _failure("Transaction handles are unavailable here")
	var path = reference.path
	if not path is String or path.is_empty() or path.length() > 512 or path.begins_with("/") or ".." in path.split("/"):
		return _failure("Node reference path is invalid")
	var node := scene_root if path == "." else scene_root.get_node_or_null(NodePath(path))
	if node == null or (node != scene_root and not scene_root.is_ancestor_of(node)):
		return _failure("Node reference target not found")
	return ErrorEnvelope.success(node)


func _decode_resource_reference(value: Dictionary, property_info: Dictionary) -> Dictionary:
	var path = value.get("path")
	if not path is String or not path.begins_with("res://") or path.length() > 512:
		return _failure("Resource reference path must be a bounded res:// path")
	if ".." in path.trim_prefix("res://").split("/") or not ResourceLoader.exists(path):
		return _failure("Resource reference not found")
	var resource := ResourceLoader.load(path)
	if resource == null:
		return _failure("Resource reference could not be loaded")
	var expected := _expected_object_class(property_info)
	if not expected.is_empty() and not resource.is_class(expected):
		return _failure("Resource class %s is not compatible with %s" % [resource.get_class(), expected])
	return ErrorEnvelope.success(resource)


func _decode_enum(value: Variant, property_info: Dictionary, flags: bool) -> Dictionary:
	var entries := _enum_entries(str(property_info.get("hint_string", "")), flags)
	if value is int:
		if not flags and not entries.values().has(value):
			return _failure("Enum value is not declared by the property")
		return ErrorEnvelope.success(value)
	if value is String and entries.has(value):
		return ErrorEnvelope.success(entries[value])
	if flags and value is Array:
		var result := 0
		for name in value:
			if not name is String or not entries.has(name):
				return _failure("Flag name is not declared by the property")
			result |= int(entries[name])
		return ErrorEnvelope.success(result)
	return _failure("Enum or flags value is invalid")


func _enum_entries(hint: String, flags: bool) -> Dictionary:
	var entries := {}
	var next_value := 1 if flags else 0
	for raw_entry in hint.split(",", false):
		var parts := raw_entry.split(":", false, 1)
		var name := parts[0].strip_edges()
		if name.is_empty():
			continue
		var entry_value := next_value
		if parts.size() == 2 and parts[1].strip_edges().is_valid_int():
			entry_value = int(parts[1].strip_edges())
		entries[name] = entry_value
		next_value = entry_value << 1 if flags else entry_value + 1
	return entries


func _decode_rect(value: Dictionary, integer: bool) -> Dictionary:
	var position := _numbers(value.get("position"), integer, "Rect position", 2)
	if not position.ok:
		return position
	var size := _numbers(value.get("size"), integer, "Rect size", 2)
	if not size.ok:
		return size
	if integer:
		return ErrorEnvelope.success(Rect2i(position.result[0], position.result[1], size.result[0], size.result[1]))
	return ErrorEnvelope.success(Rect2(position.result[0], position.result[1], size.result[0], size.result[1]))


func _decode_transform2d(value: Dictionary) -> Dictionary:
	var x := _numbers(value.get("x"), false, "Transform2D x", 2)
	var y := _numbers(value.get("y"), false, "Transform2D y", 2)
	var origin := _numbers(value.get("origin"), false, "Transform2D origin", 2)
	if not x.ok: return x
	if not y.ok: return y
	if not origin.ok: return origin
	return ErrorEnvelope.success(Transform2D(Vector2(x.result[0], x.result[1]), Vector2(y.result[0], y.result[1]), Vector2(origin.result[0], origin.result[1])))


func _decode_basis(value: Variant) -> Dictionary:
	if not value is Array or value.size() != 3:
		return _failure("Basis value must contain three vectors")
	var axes: Array[Vector3] = []
	for axis in value:
		var numbers := _numbers(axis, false, "Basis axis", 3)
		if not numbers.ok:
			return numbers
		axes.append(Vector3(numbers.result[0], numbers.result[1], numbers.result[2]))
	return ErrorEnvelope.success(Basis(axes[0], axes[1], axes[2]))


func _decode_aabb(value: Dictionary) -> Dictionary:
	var position := _numbers(value.get("position"), false, "AABB position", 3)
	var size := _numbers(value.get("size"), false, "AABB size", 3)
	if not position.ok: return position
	if not size.ok: return size
	return ErrorEnvelope.success(AABB(Vector3(position.result[0], position.result[1], position.result[2]), Vector3(size.result[0], size.result[1], size.result[2])))


func _decode_transform3d(value: Dictionary) -> Dictionary:
	var basis := _decode_basis(value.get("basis"))
	var origin := _numbers(value.get("origin"), false, "Transform3D origin", 3)
	if not basis.ok: return basis
	if not origin.ok: return origin
	return ErrorEnvelope.success(Transform3D(basis.result, Vector3(origin.result[0], origin.result[1], origin.result[2])))


func _packed_array(value: Variant, kind: String) -> Dictionary:
	if not value is Array or value.size() > Limits.MAX_PACKED_VALUE_ITEMS:
		return _failure("Packed array is invalid or exceeds its element limit")
	match kind:
		"packed_string_array":
			if value.any(func(item): return not item is String): return _failure("Packed string array contains a non-string")
			return ErrorEnvelope.success(PackedStringArray(value))
		"packed_byte_array", "packed_int32_array", "packed_int64_array":
			var integers := _numbers(value, true, kind)
			if not integers.ok: return integers
			if kind == "packed_byte_array" and integers.result.any(func(item): return item < 0 or item > 255): return _failure("Packed byte value is outside 0..255")
			if kind == "packed_byte_array": return ErrorEnvelope.success(PackedByteArray(integers.result))
			if kind == "packed_int32_array": return ErrorEnvelope.success(PackedInt32Array(integers.result))
			return ErrorEnvelope.success(PackedInt64Array(integers.result))
		"packed_float32_array", "packed_float64_array":
			var numbers := _numbers(value, false, kind)
			if not numbers.ok: return numbers
			return ErrorEnvelope.success(PackedFloat32Array(numbers.result) if kind == "packed_float32_array" else PackedFloat64Array(numbers.result))
		"packed_vector2_array", "packed_vector3_array", "packed_color_array":
			var output: Array = []
			var count := 2 if kind == "packed_vector2_array" else (4 if kind == "packed_color_array" else 3)
			for item in value:
				var components := _numbers(item, false, kind, count)
				if not components.ok: return components
				if kind == "packed_vector2_array": output.append(Vector2(components.result[0], components.result[1]))
				elif kind == "packed_vector3_array": output.append(Vector3(components.result[0], components.result[1], components.result[2]))
				else: output.append(Color(components.result[0], components.result[1], components.result[2], components.result[3]))
			if kind == "packed_vector2_array": return ErrorEnvelope.success(PackedVector2Array(output))
			if kind == "packed_vector3_array": return ErrorEnvelope.success(PackedVector3Array(output))
			return ErrorEnvelope.success(PackedColorArray(output))
	return _failure("Packed array type is unsupported")


func _validate_property_compatibility(value: Variant, target_type: int, property_info: Dictionary) -> Dictionary:
	if value == null:
		return ErrorEnvelope.success(value)
	if target_type != TYPE_OBJECT:
		if typeof(value) != target_type:
			return _failure("Decoded value does not match property type %s" % type_string(target_type))
		return ErrorEnvelope.success(value)
	if not value is Object:
		return _failure("Decoded object reference is invalid")
	var expected := _expected_object_class(property_info)
	if not expected.is_empty() and not value.is_class(expected):
		return _failure("Object class %s is not compatible with %s" % [value.get_class(), expected])
	return ErrorEnvelope.success(value)


func _expected_object_class(property_info: Dictionary) -> String:
	var expected_class := str(property_info.get("class_name", ""))
	if not expected_class.is_empty():
		return expected_class
	var hint := int(property_info.get("hint", PROPERTY_HINT_NONE))
	if hint in [PROPERTY_HINT_RESOURCE_TYPE, PROPERTY_HINT_NODE_TYPE]:
		return str(property_info.get("hint_string", "")).split(",", false)[0]
	return ""


func _validate_json_value(value: Variant, depth := 0, state := {}) -> Dictionary:
	if state.is_empty():
		state["items"] = 0
		if JSON.stringify(value).to_utf8_buffer().size() > Limits.MAX_VALUE_BYTES:
			return _failure("Value encoded size exceeds the limit")
	if depth > Limits.MAX_VALUE_DEPTH:
		return _failure("Value nesting exceeds the limit")
	state.items += 1
	if state.items > Limits.MAX_VALUE_ITEMS:
		return _failure("Value element count exceeds the limit")
	match typeof(value):
		TYPE_NIL, TYPE_BOOL, TYPE_INT:
			return ErrorEnvelope.success(true)
		TYPE_FLOAT:
			return ErrorEnvelope.success(true) if is_finite(value) else _failure("Numbers must be finite")
		TYPE_STRING:
			return ErrorEnvelope.success(true) if value.length() <= Limits.MAX_VALUE_STRING_CHARS else _failure("Value string exceeds the limit")
		TYPE_ARRAY:
			for item in value:
				var checked := _validate_json_value(item, depth + 1, state)
				if not checked.ok: return checked
			return ErrorEnvelope.success(true)
		TYPE_DICTIONARY:
			if value.size() > Limits.MAX_VALUE_KEYS:
				return _failure("Value key count exceeds the limit")
			if (
				value.get(TAG, "") is String
				and str(value.get(TAG)).begins_with("packed_")
				and value.get("values") is Array
			):
				if value.values.size() > Limits.MAX_PACKED_VALUE_ITEMS:
					return _failure("Packed array exceeds the element limit")
				for item in value.values:
					var primitive := _validate_packed_input(item, depth + 1)
					if not primitive.ok: return primitive
				return ErrorEnvelope.success(true)
			for key in value:
				if not key is String or key.length() > 128:
					return _failure("Value object keys must be bounded strings")
				var checked := _validate_json_value(value[key], depth + 1, state)
				if not checked.ok: return checked
			return ErrorEnvelope.success(true)
	return _failure("Value contains a non-JSON input type")


func _encode_value(value: Variant, property_info: Dictionary, scene_root: Node, depth: int) -> Variant:
	if depth >= Limits.MAX_VALUE_DEPTH:
		return {TAG: "truncated", "reason": "depth"}
	match typeof(value):
		TYPE_NIL, TYPE_BOOL, TYPE_INT:
			return value
		TYPE_FLOAT:
			return value if is_finite(value) else null
		TYPE_STRING:
			return value.left(Limits.MAX_VALUE_STRING_CHARS)
		TYPE_STRING_NAME:
			return {TAG: "string_name", "value": str(value).left(Limits.MAX_VALUE_STRING_CHARS)}
		TYPE_NODE_PATH:
			return {TAG: "node_path", "path": str(value).left(512)}
		TYPE_VECTOR2, TYPE_VECTOR2I:
			return [value.x, value.y]
		TYPE_VECTOR3, TYPE_VECTOR3I:
			return [value.x, value.y, value.z]
		TYPE_VECTOR4, TYPE_VECTOR4I:
			return [value.x, value.y, value.z, value.w]
		TYPE_COLOR:
			return [value.r, value.g, value.b, value.a]
		TYPE_RECT2, TYPE_RECT2I:
			return {TAG: "rect2i" if value is Rect2i else "rect2", "position": [value.position.x, value.position.y], "size": [value.size.x, value.size.y]}
		TYPE_TRANSFORM2D:
			return {TAG: "transform2d", "x": [value.x.x, value.x.y], "y": [value.y.x, value.y.y], "origin": [value.origin.x, value.origin.y]}
		TYPE_PLANE:
			return {TAG: "plane", "value": [value.x, value.y, value.z, value.d]}
		TYPE_QUATERNION:
			return {TAG: "quaternion", "value": [value.x, value.y, value.z, value.w]}
		TYPE_AABB:
			return {TAG: "aabb", "position": [value.position.x, value.position.y, value.position.z], "size": [value.size.x, value.size.y, value.size.z]}
		TYPE_BASIS:
			return {TAG: "basis", "value": [[value.x.x, value.x.y, value.x.z], [value.y.x, value.y.y, value.y.z], [value.z.x, value.z.y, value.z.z]]}
		TYPE_TRANSFORM3D:
			return {TAG: "transform3d", "basis": [[value.basis.x.x, value.basis.x.y, value.basis.x.z], [value.basis.y.x, value.basis.y.y, value.basis.y.z], [value.basis.z.x, value.basis.z.y, value.basis.z.z]], "origin": [value.origin.x, value.origin.y, value.origin.z]}
		TYPE_ARRAY, TYPE_PACKED_BYTE_ARRAY, TYPE_PACKED_INT32_ARRAY, TYPE_PACKED_INT64_ARRAY, TYPE_PACKED_FLOAT32_ARRAY, TYPE_PACKED_FLOAT64_ARRAY, TYPE_PACKED_STRING_ARRAY, TYPE_PACKED_VECTOR2_ARRAY, TYPE_PACKED_VECTOR3_ARRAY, TYPE_PACKED_COLOR_ARRAY:
			return _encode_array(value, property_info, scene_root, depth)
		TYPE_DICTIONARY:
			var dictionary := {}
			for key in value.keys().slice(0, Limits.MAX_VALUE_KEYS):
				dictionary[str(key).left(128)] = _encode_value(value[key], {}, scene_root, depth + 1)
			return dictionary
		TYPE_OBJECT:
			if value == null: return null
			if value is Node and scene_root != null and (value == scene_root or scene_root.is_ancestor_of(value)):
				return {TAG: "node", "path": "." if value == scene_root else str(scene_root.get_path_to(value))}
			if value is Resource and not value.resource_path.is_empty():
				return {TAG: "resource", "path": value.resource_path, "class": value.get_class()}
			return {TAG: "object", "class": value.get_class()}
	return {TAG: "unsupported", "class": type_string(typeof(value))}


func _encode_array(value: Variant, property_info: Dictionary, scene_root: Node, depth: int) -> Variant:
	var output: Array = []
	for item in value.slice(0, Limits.MAX_PACKED_VALUE_ITEMS):
		output.append(_encode_value(item, {}, scene_root, depth + 1))
	if value is Array:
		return output
	var packed_tags := {
		TYPE_PACKED_BYTE_ARRAY: "packed_byte_array",
		TYPE_PACKED_INT32_ARRAY: "packed_int32_array",
		TYPE_PACKED_INT64_ARRAY: "packed_int64_array",
		TYPE_PACKED_FLOAT32_ARRAY: "packed_float32_array",
		TYPE_PACKED_FLOAT64_ARRAY: "packed_float64_array",
		TYPE_PACKED_STRING_ARRAY: "packed_string_array",
		TYPE_PACKED_VECTOR2_ARRAY: "packed_vector2_array",
		TYPE_PACKED_VECTOR3_ARRAY: "packed_vector3_array",
		TYPE_PACKED_COLOR_ARRAY: "packed_color_array",
	}
	return {TAG: packed_tags[typeof(value)], "values": output}


func _validate_packed_input(value: Variant, depth: int) -> Dictionary:
	if depth > Limits.MAX_VALUE_DEPTH: return _failure("Packed value nesting exceeds the limit")
	if value is String:
		return ErrorEnvelope.success(true) if value.length() <= Limits.MAX_VALUE_STRING_CHARS else _failure("Packed string exceeds the limit")
	if value is int: return ErrorEnvelope.success(true)
	if value is float: return ErrorEnvelope.success(true) if is_finite(value) else _failure("Packed numbers must be finite")
	if value is Array:
		if value.size() > 4: return _failure("Packed vector component count exceeds the limit")
		for component in value:
			if not (component is int or component is float) or not is_finite(float(component)):
				return _failure("Packed vector components must be finite numbers")
		return ErrorEnvelope.success(true)
	return _failure("Packed array contains an unsupported value")


func _vector(value: Variant, count: int, integer: bool, label: String, constructor: Callable) -> Dictionary:
	var components := _numbers(value, integer, label, count)
	return components if not components.ok else ErrorEnvelope.success(constructor.call(components.result))


func _numbers(value: Variant, integer: bool, label: String, expected := -1) -> Dictionary:
	if not value is Array or (expected >= 0 and value.size() != expected):
		return _failure("%s must be a %d-element array" % [label, expected])
	var output: Array = []
	for component in value:
		if not (component is int or component is float) or component is bool or not is_finite(float(component)):
			return _failure("%s components must be finite numbers" % label)
		if integer and component is float and component != floor(component):
			return _failure("%s components must be integers" % label)
		output.append(int(component) if integer else float(component))
	return ErrorEnvelope.success(output)


func _tagged_string(value: Dictionary, field: String, tagged_type: int, target_type: int) -> Dictionary:
	if target_type != tagged_type:
		return _type_mismatch(str(value.get(TAG)), target_type)
	var text = value.get(field)
	if not text is String:
		return _failure("Tagged string value is invalid")
	return ErrorEnvelope.success(StringName(text))


func _type_mismatch(kind: String, target_type: int) -> Dictionary:
	return _failure("Tagged %s value does not match property type %s" % [kind, type_string(target_type)])


func _make_vector2(value: Array) -> Vector2: return Vector2(value[0], value[1])
func _make_vector2i(value: Array) -> Vector2i: return Vector2i(value[0], value[1])
func _make_vector3(value: Array) -> Vector3: return Vector3(value[0], value[1], value[2])
func _make_vector3i(value: Array) -> Vector3i: return Vector3i(value[0], value[1], value[2])
func _make_vector4(value: Array) -> Vector4: return Vector4(value[0], value[1], value[2], value[3])
func _make_vector4i(value: Array) -> Vector4i: return Vector4i(value[0], value[1], value[2], value[3])


func _failure(message: String) -> Dictionary:
	return ErrorEnvelope.failure(message, ErrorEnvelope.INVALID_ARGUMENT)
