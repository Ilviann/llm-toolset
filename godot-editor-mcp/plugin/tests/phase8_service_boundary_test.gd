extends SceneTree

const EditedSceneInspector := preload("../addons/godot_mcp/edited_scene_inspector.gd")
const SceneCommands := preload("../addons/godot_mcp/scene_commands.gd")
const SceneTransaction := preload("../addons/godot_mcp/scene_transaction.gd")


func _init() -> void:
	var inspector = EditedSceneInspector.new(null, null, null, null, null)
	var mutations = SceneCommands.new(null, null, null, null, null)
	var transactions = SceneTransaction.new(null, null, null, null)
	var inspection_handlers: Dictionary = inspector.handlers()
	var mutation_handlers: Dictionary = mutations.handlers()
	var transaction_handlers: Dictionary = transactions.handlers()
	assert(inspection_handlers.keys() == ["tree", "inspect"])
	assert(
		mutation_handlers.keys()
		== ["create_scene", "add_node", "instantiate_scene", "set_property", "select"]
	)
	assert(transaction_handlers.keys() == ["scene_transaction"])
	for command in inspection_handlers:
		assert(not mutation_handlers.has(command))
		assert((inspection_handlers[command] as Callable).is_valid())
	for command in mutation_handlers:
		assert((mutation_handlers[command] as Callable).is_valid())
	assert((transaction_handlers.scene_transaction as Callable).is_valid())

	var inspector_source := FileAccess.get_file_as_string(
		"res://addons/godot_mcp/edited_scene_inspector.gd",
	)
	var mutation_source := FileAccess.get_file_as_string(
		"res://addons/godot_mcp/scene_commands.gd",
	)
	var transaction_source := FileAccess.get_file_as_string(
		"res://addons/godot_mcp/scene_transaction.gd",
	)
	assert("create_action(" not in inspector_source)
	assert("commit_action(" not in inspector_source)
	assert("_collect_nodes" in inspector_source)
	assert("_tree_snapshot" in inspector_source)
	assert("_cursor_offset" in inspector_source)
	assert("_collect_nodes" not in mutation_source)
	assert("_tree_snapshot" not in mutation_source)
	assert("_cursor_offset" not in mutation_source)
	assert("_cursors" not in mutation_source)
	assert("create_action(" not in mutation_source)
	assert("commit_action(" in transaction_source)
	assert("scene_transaction" not in inspection_handlers)

	print("phase8_service_boundary_test: PASS")
	quit()
