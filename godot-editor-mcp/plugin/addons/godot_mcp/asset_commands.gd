extends "command_base.gd"

const Limits := preload("command_limits.gd")
const MAX_ASSETS := Limits.MAX_ASSETS
const MAX_ASSET_SCAN := Limits.MAX_ASSET_SCAN
const CREATABLE_RESOURCE_TYPES := [
	"StandardMaterial3D", "ORMMaterial3D", "ShaderMaterial", "Environment",
	"Gradient", "Curve", "StyleBoxFlat", "AudioStreamRandomizer",
]


func handlers() -> Dictionary:
	return {
		"assets": Callable(self, "_list_assets"),
		"asset_info": Callable(self, "_asset_info"),
		"scan_asset": Callable(self, "_scan_asset"),
		"create_resource": Callable(self, "_create_resource"),
		"create_scene": Callable(self, "_create_scene"),
		"open_scene": Callable(self, "_open_scene"),
	}


func _list_assets(arguments: Dictionary) -> Dictionary:
	var folder_value = arguments.get("folder", ".")
	var folder_path := "res://"
	if folder_value != ".":
		var checked := _project_path(folder_value)
		if not checked.ok:
			return checked
		folder_path = checked.result
	var asset_type = arguments.get("type", "all")
	var allowed_types := ["all", "scene", "script", "image", "model", "audio", "font", "material", "resource"]
	if not asset_type is String or asset_type not in allowed_types:
		return _failure("Type filter is invalid")
	var limit_value = arguments.get("limit", 50)
	if not limit_value is int and not limit_value is float:
		return _failure("Limit must be a number")
	var limit := int(limit_value)
	if limit < 1 or limit > MAX_ASSETS:
		return _failure("Limit must be between 1 and 100")

	var filesystem := get_editor_interface().get_resource_filesystem()
	var directory := filesystem.get_filesystem_path(folder_path)
	if directory == null:
		return _failure("Asset folder not found")
	var output: Array[Dictionary] = []
	var state := {"visited": 0, "truncated": false}
	_collect_assets(directory, asset_type, limit, output, state)
	return _success({"assets": output, "truncated": state.truncated})


func _collect_assets(
	directory: EditorFileSystemDirectory,
	asset_type: String,
	limit: int,
	output: Array[Dictionary],
	state: Dictionary,
) -> void:
	var raw_directory := DirAccess.open(directory.get_path())
	for index in directory.get_file_count():
		state.visited += 1
		if state.visited > MAX_ASSET_SCAN:
			state.truncated = true
			return
		var file_name := directory.get_file(index)
		if raw_directory != null and raw_directory.is_link(file_name):
			continue
		var path := directory.get_path().path_join(file_name)
		var resource_type := directory.get_file_type(index)
		var category := _asset_category(path, resource_type)
		if asset_type == "all" or category == asset_type or (asset_type == "resource" and category == "resource"):
			if output.size() >= limit:
				state.truncated = true
				return
			output.append({"path": path, "type": resource_type, "category": category})
	for index in directory.get_subdir_count():
		if state.truncated:
			return
		var subdirectory := directory.get_subdir(index)
		if raw_directory != null and raw_directory.is_link(subdirectory.get_path().get_file()):
			continue
		_collect_assets(subdirectory, asset_type, limit, output, state)


func _asset_info(arguments: Dictionary) -> Dictionary:
	var checked := _project_path(arguments.get("path"))
	if not checked.ok:
		return checked
	var path := checked.result as String
	if not FileAccess.file_exists(path):
		return _failure("Asset not found")
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return _failure("Cannot read asset information")
	var size := file.get_length()
	var resource_type: String = get_editor_interface().get_resource_filesystem().get_file_type(path)
	if resource_type.is_empty() and path.get_extension().to_lower() in ["tres", "res", "tscn", "scn"]:
		var loaded := ResourceLoader.load(path)
		if loaded != null:
			resource_type = loaded.get_class()
	var dependencies: Array[String] = []
	for dependency in ResourceLoader.get_dependencies(path):
		var parts := str(dependency).split("::")
		dependencies.append(parts[parts.size() - 1])
		if dependencies.size() >= 50:
			break
	return _success({
		"path": path,
		"type": resource_type,
		"category": _asset_category(path, resource_type),
		"bytes": size,
		"imported": FileAccess.file_exists(path + ".import"),
		"loadable": ResourceLoader.exists(path),
		"dependencies": dependencies,
		"dependencies_truncated": dependencies.size() >= 50,
	})


func _scan_asset(arguments: Dictionary) -> Dictionary:
	var checked := _project_path(arguments.get("path"))
	if not checked.ok:
		return checked
	var filesystem := get_editor_interface().get_resource_filesystem()
	if filesystem.is_scanning():
		return _success({"path": checked.result, "scan": "already_running", "operation_id": null})
	var operation_id = _accept_operation("filesystem_scan", {"path": checked.result})
	if _state_monitor != null:
		_state_monitor.track_import(checked.result, operation_id)
	filesystem.scan()
	return _success({"path": checked.result, "scan": "queued", "operation_id": operation_id})


func _create_resource(arguments: Dictionary) -> Dictionary:
	var checked := _project_path(arguments.get("path"), true, PackedStringArray(["tres"]))
	if not checked.ok:
		return checked
	var path := checked.result as String
	if FileAccess.file_exists(path):
		return _failure("Resource already exists")
	if not DirAccess.dir_exists_absolute(ProjectSettings.globalize_path(path.get_base_dir())):
		return _failure("Resource folder does not exist")
	var resource_type = arguments.get("type")
	if not resource_type is String or resource_type not in CREATABLE_RESOURCE_TYPES:
		return _failure("Resource type is not allowed")
	var properties = arguments.get("properties", {})
	if not properties is Dictionary or properties.size() > 32:
		return _failure("Properties must be an object with at most 32 entries")
	var object = ClassDB.instantiate(resource_type)
	if not object is Resource:
		return _failure("Could not instantiate resource")
	var resource := object as Resource
	for property_name in properties:
		if not property_name is String or property_name.is_empty() or property_name.length() > 128:
			return _failure("Resource property name is invalid")
		var property_info: Dictionary = {}
		for info in resource.get_property_list():
			if str(info.name) == property_name and (int(info.usage) & PROPERTY_USAGE_EDITOR) != 0:
				property_info = info
				break
		if property_info.is_empty():
			return _failure("Editable resource property not found: %s" % property_name)
		var converted := _convert_value(properties[property_name], int(property_info.type))
		if not converted.ok:
			return _failure("Invalid %s: %s" % [property_name, _error_message(converted)])
		resource.set(property_name, converted.result)
	var save_error := ResourceSaver.save(resource, path)
	if save_error != OK:
		return _failure("Could not save resource")
	get_editor_interface().get_resource_filesystem().update_file(path)
	return _success({"path": path, "type": resource_type, "properties": properties.keys()})


func _create_scene(arguments: Dictionary) -> Dictionary:
	var checked := _project_path(arguments.get("path"), true, PackedStringArray(["tscn"]))
	if not checked.ok:
		return checked
	var path := checked.result as String
	if FileAccess.file_exists(path):
		return _failure("Scene already exists")
	var parent_folder := path.get_base_dir()
	if not DirAccess.dir_exists_absolute(ProjectSettings.globalize_path(parent_folder)):
		return _failure("Scene folder does not exist")
	var root_type = arguments.get("root_type")
	if not root_type is String or root_type.is_empty() or root_type.length() > 128:
		return _failure("Root type must be a non-empty string up to 128 characters")
	if not ClassDB.class_exists(root_type) or not ClassDB.can_instantiate(root_type) or not ClassDB.is_parent_class(root_type, "Node"):
		return _failure("Root type must be an instantiable built-in Node class")
	var valid_name := _checked_node_name(arguments.get("root_name"))
	if not valid_name.ok:
		return valid_name
	var root_object = ClassDB.instantiate(root_type)
	if not root_object is Node:
		if root_object != null:
			root_object.free()
		return _failure("Could not instantiate root node")
	var root := root_object as Node
	root.name = valid_name.result
	var packed := PackedScene.new()
	var pack_error := packed.pack(root)
	if pack_error != OK:
		root.free()
		return _failure("Could not pack scene")
	var save_error := ResourceSaver.save(packed, path)
	root.free()
	if save_error != OK:
		return _failure("Could not save scene")
	get_editor_interface().get_resource_filesystem().update_file(path)
	return _success({"path": path, "root_type": root_type, "root_name": valid_name.result})


func _open_scene(arguments: Dictionary) -> Dictionary:
	var checked := _project_path(arguments.get("path"), false, PackedStringArray(["tscn", "scn"]))
	if not checked.ok:
		return checked
	var path := checked.result as String
	if not FileAccess.file_exists(path):
		return _failure("PackedScene not found")
	var resource := ResourceLoader.load(path)
	if not resource is PackedScene:
		return _failure("PackedScene not found")
	get_editor_interface().get_resource_filesystem().update_file(path)
	get_editor_interface().open_scene_from_path(path)
	var operation_id = _accept_operation("open_scene", {"path": path})
	return _success({"path": path, "open": "requested", "operation_id": operation_id})


func _asset_category(path: String, resource_type: String) -> String:
	var extension := path.get_extension().to_lower()
	if extension in ["tscn", "scn"]:
		return "scene"
	if extension in ["gd", "cs"]:
		return "script"
	if extension in ["bmp", "exr", "hdr", "jpeg", "jpg", "png", "svg", "webp"]:
		return "image"
	if extension in ["fbx", "glb", "gltf", "obj"]:
		return "model"
	if extension in ["mp3", "ogg", "wav"]:
		return "audio"
	if extension in ["otf", "ttf", "woff", "woff2"]:
		return "font"
	if "Material" in resource_type or extension in ["material", "shader"]:
		return "material"
	return "resource"
