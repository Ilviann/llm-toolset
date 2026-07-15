extends SceneTree

const AssetCommands := preload("../addons/godot_mcp/asset_commands.gd")
const AtomicJsonRecord := preload("../addons/godot_mcp/atomic_json_record.gd")
const EditedSceneInspector := preload("../addons/godot_mcp/edited_scene_inspector.gd")
const InputEventCodec := preload("../addons/godot_mcp/input_event_codec.gd")
const InputMapCommands := preload("../addons/godot_mcp/input_map_commands.gd")
const ProjectIdentity := preload("../addons/godot_mcp/project_identity.gd")
const ProjectPathGuard := preload("../addons/godot_mcp/project_path_guard.gd")
const ProjectSettingsCommands := preload("../addons/godot_mcp/project_settings_commands.gd")
const PropertyValueCodec := preload("../addons/godot_mcp/property_value_codec.gd")
const SceneCommands := preload("../addons/godot_mcp/scene_commands.gd")
const SceneNodeAccess := preload("../addons/godot_mcp/scene_node_access.gd")


func _init() -> void:
	_test_project_identity_platform_branches()
	_test_atomic_json_record_bounds_and_replacement()
	_test_focused_codecs_and_path_guard()
	# Loading every command and helper script above is also a parser-level guard
	# for their narrowed constructor and dependency surfaces.
	assert(AssetCommands != null and EditedSceneInspector != null and SceneCommands != null)
	assert(ProjectSettingsCommands != null and InputMapCommands != null)
	assert(SceneNodeAccess != null)
	print("phase5_infrastructure_test: PASS")
	quit()


func _test_project_identity_platform_branches() -> void:
	assert(ProjectIdentity.normalized_path("/tmp/Game/", "Linux") == "/tmp/Game")
	assert(ProjectIdentity.normalized_path("C:\\Games\\Demo\\", "Windows") == "c:/games/demo")
	assert(
		ProjectIdentity.hash_path("C:\\Games\\Demo\\", "Windows")
		== ProjectIdentity.hash_path("c:/games/demo", "Linux")
	)


func _test_atomic_json_record_bounds_and_replacement() -> void:
	var path := "user://godot_mcp_phase5_record.json"
	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))
	assert(AtomicJsonRecord.write(path, {"owner": 1, "value": "first"}) == OK)
	var first := AtomicJsonRecord.read(path, 256)
	assert(first.ok and first.value.owner == 1 and first.value.value == "first")
	assert(AtomicJsonRecord.write(path, {"owner": 2, "value": "second"}) == OK)
	var second := AtomicJsonRecord.read(path, 256)
	assert(second.ok and second.value.owner == 2 and second.value.value == "second")
	var bounded := AtomicJsonRecord.read(path, 4)
	assert(not bounded.ok and bounded.error == "size")
	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))


func _test_focused_codecs_and_path_guard() -> void:
	var values = PropertyValueCodec.new()
	var converted: Dictionary = values.convert([1, 2], TYPE_VECTOR2)
	assert(converted.ok and converted.result == Vector2(1, 2))
	assert(values.encode(Color(1, 0.5, 0.25, 1)) == [1.0, 0.5, 0.25, 1.0])
	var events = InputEventCodec.new()
	var decoded: Dictionary = events.decode({"type": "key", "key": "Space"})
	assert(decoded.ok and events.normalize(decoded.result).type == "key")
	var paths = ProjectPathGuard.new()
	assert(paths.check("scenes/main.tscn", false, PackedStringArray(["tscn"])).ok)
	assert(not paths.check("../outside.tscn").ok)
