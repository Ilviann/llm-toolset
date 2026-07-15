extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")
const Limits := preload("command_limits.gd")
const MAX_ASSETS := Limits.MAX_ASSETS
const MAX_ASSET_SCAN := Limits.MAX_ASSET_SCAN
const CREATABLE_RESOURCE_TYPES := [
	"StandardMaterial3D", "ORMMaterial3D", "ShaderMaterial", "Environment",
	"Gradient", "Curve", "StyleBoxFlat", "AudioStreamRandomizer",
]

var _editor_interface: EditorInterface
var _operations: RefCounted
var _track_import: Callable
var _filesystem_generation: Callable
var _project_paths: RefCounted
var _scene_nodes: RefCounted
var _property_values: RefCounted
var _cursors: RefCounted


func _init(
	editor_interface: EditorInterface,
	operations: RefCounted,
	track_import: Callable,
	filesystem_generation: Callable,
	project_paths: RefCounted,
	scene_nodes: RefCounted,
	property_values: RefCounted,
	cursors: RefCounted,
) -> void:
	_editor_interface = editor_interface
	_operations = operations
	_track_import = track_import
	_filesystem_generation = filesystem_generation
	_project_paths = project_paths
	_scene_nodes = scene_nodes
	_property_values = property_values
	_cursors = cursors


func handlers() -> Dictionary:
	return {
		"assets": Callable(self, "_list_assets"),
		"asset_info": Callable(self, "_asset_info"),
		"scan_asset": Callable(self, "_scan_asset"),
		"create_resource": Callable(self, "_create_resource"),
		"open_scene": Callable(self, "_open_scene"),
	}


func _list_assets(arguments: Dictionary) -> Dictionary:
	var folder_value = arguments.get("folder", ".")
	var folder_path := "res://"
	if folder_value != ".":
		var checked: Dictionary = _project_paths.check(folder_value)
		if not checked.ok:
			return checked
		folder_path = checked.result
	var asset_type = arguments.get("type", "all")
	var allowed_types := ["all", "scene", "script", "image", "model", "audio", "font", "material", "resource"]
	if not asset_type is String or asset_type not in allowed_types:
		return _failure("Type filter is invalid")
	var limit_value = arguments.get("limit", 50)
	if (not limit_value is int and not limit_value is float) or float(limit_value) != floorf(float(limit_value)):
		return _failure("Limit must be an integer")
	var limit := int(limit_value)
	if limit < 1 or limit > MAX_ASSETS:
		return _failure("Limit must be between 1 and 100")

	var filesystem := _editor_interface.get_resource_filesystem()
	var directory := filesystem.get_filesystem_path(folder_path)
	if directory == null:
		return _failure("Asset folder not found")
	var generation := 0
	if _filesystem_generation.is_valid():
		generation = int(_filesystem_generation.call())
	var query := [folder_path, asset_type, limit]
	var snapshot: String = _cursors.snapshot_id("assets", generation)
	var offset := 0
	if arguments.has("cursor"):
		var resumed: Dictionary = _cursors.resume(
			arguments.cursor, "assets", query, snapshot,
		)
		if not resumed.ok:
			return resumed
		offset = int(resumed.result)
	var output: Array[Dictionary] = []
	var state := {
		"visited": 0, "matched": 0, "has_more": false, "scan_exhausted": false,
	}
	_collect_assets(directory, asset_type, offset, limit, output, state)
	var continuation_available: bool = state.has_more
	var cursor: Variant = null
	if continuation_available:
		cursor = _cursors.issue("assets", query, snapshot, offset + output.size())
	return _success({
		"assets": output,
		"truncated": continuation_available or state.scan_exhausted,
		"snapshot_id": snapshot,
		"continuation_available": continuation_available,
		"cursor": cursor,
	})


func _collect_assets(
	directory: EditorFileSystemDirectory,
	asset_type: String,
	offset: int,
	limit: int,
	output: Array[Dictionary],
	state: Dictionary,
) -> bool:
	var raw_directory := DirAccess.open(directory.get_path())
	for index in directory.get_file_count():
		if state.visited >= MAX_ASSET_SCAN:
			state.scan_exhausted = true
			return true
		state.visited += 1
		var file_name := directory.get_file(index)
		if raw_directory != null and raw_directory.is_link(file_name):
			continue
		var path := directory.get_path().path_join(file_name)
		var resource_type := directory.get_file_type(index)
		var category := _asset_category(path, resource_type)
		if asset_type == "all" or category == asset_type or (asset_type == "resource" and category == "resource"):
			if state.matched < offset:
				state.matched += 1
			elif output.size() < limit:
				output.append({"path": path, "type": resource_type, "category": category})
				state.matched += 1
			else:
				state.has_more = true
				return true
	for index in directory.get_subdir_count():
		var subdirectory := directory.get_subdir(index)
		if raw_directory != null and raw_directory.is_link(subdirectory.get_path().get_file()):
			continue
		if _collect_assets(subdirectory, asset_type, offset, limit, output, state):
			return true
	return false


func _asset_info(arguments: Dictionary) -> Dictionary:
	var checked: Dictionary = _project_paths.check(arguments.get("path"))
	if not checked.ok:
		return checked
	var path := checked.result as String
	if not FileAccess.file_exists(path):
		return _failure("Asset not found")
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return _failure("Cannot read asset information")
	var size := file.get_length()
	var resource_type: String = _editor_interface.get_resource_filesystem().get_file_type(path)
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
	var checked: Dictionary = _project_paths.check(arguments.get("path"))
	if not checked.ok:
		return checked
	var filesystem := _editor_interface.get_resource_filesystem()
	if filesystem.is_scanning():
		return _success({"path": checked.result, "scan": "already_running", "operation_id": null})
	var operation_id = _operations.accept("filesystem_scan", {"path": checked.result})
	if _track_import.is_valid():
		_track_import.call(checked.result, operation_id)
	filesystem.scan()
	return _success({"path": checked.result, "scan": "queued", "operation_id": operation_id})


func _create_resource(arguments: Dictionary) -> Dictionary:
	var checked: Dictionary = _project_paths.check(arguments.get("path"), true, PackedStringArray(["tres"]))
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
		var converted: Dictionary = _property_values.convert(
			properties[property_name], int(property_info.type), property_info,
		)
		if not converted.ok:
			return _failure("Invalid %s: %s" % [property_name, ErrorEnvelope.message(converted)])
		resource.set(property_name, converted.result)
	var save_error := ResourceSaver.save(resource, path)
	if save_error != OK:
		return _failure("Could not save resource")
	_editor_interface.get_resource_filesystem().update_file(path)
	return _success({"path": path, "type": resource_type, "properties": properties.keys()})


func _open_scene(arguments: Dictionary) -> Dictionary:
	var checked: Dictionary = _project_paths.check(arguments.get("path"), false, PackedStringArray(["tscn", "scn"]))
	if not checked.ok:
		return checked
	var path := checked.result as String
	if not FileAccess.file_exists(path):
		return _failure("PackedScene not found")
	var resource := ResourceLoader.load(path)
	if not resource is PackedScene:
		return _failure("PackedScene not found")
	_editor_interface.get_resource_filesystem().update_file(path)
	_editor_interface.open_scene_from_path(path)
	var operation_id = _operations.accept("open_scene", {"path": path})
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


func _success(result: Variant) -> Dictionary:
	return ErrorEnvelope.success(result)


func _failure(message: String) -> Dictionary:
	return ErrorEnvelope.failure(message)
