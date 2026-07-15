extends SceneTree

const ErrorEnvelope := preload("../addons/godot_mcp/error_envelope.gd")
const RuntimeDebuggerGateway := preload("../addons/godot_mcp/runtime_debugger_gateway.gd")
const RuntimeProbe := preload("../addons/godot_mcp/runtime_probe.gd")
const RuntimeSceneInspector := preload("../addons/godot_mcp/runtime_scene_inspector.gd")


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_probe_is_debugger_only()
	_test_runtime_tree_and_properties()
	print("phase9_runtime_inspection_test: PASS")
	quit()


func _test_probe_is_debugger_only() -> void:
	assert(RuntimeDebuggerGateway != null)
	assert(RuntimeSceneInspector != null)
	var source := FileAccess.get_file_as_string(
		"res://addons/godot_mcp/runtime_probe.gd",
	)
	assert("if not EngineDebugger.is_active()" in source)
	assert("register_message_capture" in source)
	assert("unregister_message_capture" in source)
	assert("TCPServer" not in source)
	assert("PacketPeer" not in source)
	assert("Expression.new" not in source)
	assert("GDScript.new" not in source)
func _test_runtime_tree_and_properties() -> void:
	var scene := Node2D.new()
	scene.name = "RuntimeMain"
	root.add_child(scene)
	current_scene = scene
	var spawned := Node2D.new()
	spawned.name = "Spawned"
	spawned.process_priority = 42
	spawned.add_to_group("enemies")
	scene.add_child(spawned)
	var hidden := Sprite2D.new()
	hidden.name = "Hidden"
	hidden.visible = false
	spawned.add_child(hidden)

	var probe = RuntimeProbe.new()
	probe.name = "GodotMCPRuntimeProbe"
	root.add_child(probe)
	assert(probe.get("_project_hash") == "")
	assert(probe.get("_instance_nonce") == "")
	probe.set_process(false)
	probe.set("_run_id", 4)
	probe.set("_debugger_session_id", 7)
	probe.set("_instance_nonce", "d".repeat(32))

	var tree_response: Dictionary = probe._scene_tree({"max_depth": 4, "limit": 2})
	assert(tree_response.ok)
	var tree: Dictionary = tree_response.result
	assert(tree.scope == "runtime")
	assert(tree.nodes.size() == 2)
	assert(tree.continuation_available)
	assert(tree.nodes[1].path == "Spawned")
	assert(tree.nodes[1].runtime_id.length() == 64)
	assert(tree.nodes[1].groups == ["enemies"])
	assert(not tree.nodes.any(func(item): return item.path == "GodotMCPRuntimeProbe"))

	var runtime_id: String = tree.nodes[1].runtime_id
	var inspected: Dictionary = probe._inspect_node({
		"path": "Spawned", "runtime_id": runtime_id,
		"property": "process_priority",
	})
	assert(inspected.ok)
	assert(inspected.result.scope == "runtime")
	assert(inspected.result.properties.size() == 1)
	assert(inspected.result.properties[0].value == 42)
	var stale_id: Dictionary = probe._inspect_node({
		"path": "Spawned", "runtime_id": "e".repeat(64),
	})
	assert(not stale_id.ok)
	assert(stale_id.error.code == ErrorEnvelope.STALE_RUNTIME_ID)

	var snapshot: String = tree.snapshot_id
	var replacement := Node.new()
	replacement.name = "LaterSpawn"
	scene.add_child(replacement)
	var stale_cursor: Dictionary = probe._scene_tree({
		"max_depth": 4, "limit": 2, "_expected_snapshot": snapshot,
	})
	assert(not stale_cursor.ok)
	assert(stale_cursor.error.code == ErrorEnvelope.STALE_CURSOR)

	probe.queue_free()
	scene.queue_free()
