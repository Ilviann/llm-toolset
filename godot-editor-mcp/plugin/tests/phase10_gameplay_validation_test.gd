extends SceneTree

const ErrorEnvelope := preload("../addons/godot_mcp/error_envelope.gd")
const RuntimeGameplayCommands := preload("../addons/godot_mcp/runtime_gameplay_commands.gd")
const RuntimeProbe := preload("../addons/godot_mcp/runtime_probe.gd")

const ACTION := "godot_mcp_phase10_jump"


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var scene := Node2D.new()
	scene.name = "GameplayMain"
	root.add_child(scene)
	current_scene = scene
	var actor := Node2D.new()
	actor.name = "Actor"
	actor.process_priority = 42
	actor.add_to_group("actors")
	scene.add_child(actor)

	var probe = RuntimeProbe.new()
	probe.name = "GodotMCPRuntimeProbe"
	root.add_child(probe)
	probe.set_process(false)
	probe.set("_run_id", 10)
	probe.set("_debugger_session_id", 20)
	probe.set("_instance_nonce", "f".repeat(32))

	_test_runtime_conditions(probe)
	_test_injected_input(probe)
	_test_capture(probe)
	_test_gameplay_service_contract()

	probe.queue_free()
	scene.queue_free()
	print("phase10_gameplay_validation_test: PASS")
	quit()


func _test_runtime_conditions(probe) -> void:
	var exists: Dictionary = probe._evaluate_condition({
		"scope": "runtime", "run_id": 10, "condition": "node_exists",
		"path": "Actor", "exists": true,
	})
	assert(exists.ok)
	assert(exists.result.matched)
	assert(exists.result.evidence.exists)

	var absent: Dictionary = probe._evaluate_condition({
		"scope": "runtime", "run_id": 10, "condition": "node_exists",
		"path": "Missing", "exists": false,
	})
	assert(absent.ok)
	assert(absent.result.matched)

	var count: Dictionary = probe._evaluate_condition({
		"scope": "runtime", "run_id": 10, "condition": "node_count",
		"path": ".", "group": "actors", "max_depth": 4,
		"comparison": "eq", "value": 1,
	})
	assert(count.ok)
	assert(count.result.matched)
	assert(count.result.evidence.count == 1)
	assert(count.result.evidence.paths == ["Actor"])

	var property: Dictionary = probe._evaluate_condition({
		"scope": "runtime", "run_id": 10, "condition": "property",
		"path": "Actor", "property": "process_priority",
		"comparison": "gte", "value": 40,
	})
	assert(property.ok)
	assert(property.result.matched)
	assert(property.result.evidence.actual == 42)

	var stale: Dictionary = probe._validate_condition({
		"scope": "runtime", "run_id": 11, "condition": "node_exists",
		"path": ".",
	})
	assert(not stale.ok)
	assert(stale.error.code == ErrorEnvelope.STALE_RUNTIME_ID)


func _test_injected_input(probe) -> void:
	if not InputMap.has_action(ACTION):
		InputMap.add_action(ACTION)
	var pressed: Dictionary = probe._send_input({
		"run_id": 10, "action": ACTION, "pressed": true,
		"strength": 0.75, "frames": 2,
	})
	assert(pressed.ok)
	assert(pressed.result.injected)
	assert(Input.is_action_pressed(ACTION))
	assert(probe.get("_active_inputs").size() == 1)

	var active: Dictionary = probe.get("_active_inputs")
	var hold: Dictionary = active[ACTION]
	hold.release_msec = Time.get_ticks_msec()
	active[ACTION] = hold
	probe.set("_active_inputs", active)
	probe._poll_inputs()
	assert(probe.get("_active_inputs").is_empty())

	probe._send_input({"run_id": 10, "action": ACTION, "duration_ms": 1000})
	probe._release_all_inputs("test_cleanup")
	assert(probe.get("_active_inputs").is_empty())
	Input.action_release(ACTION)
	InputMap.erase_action(ACTION)


func _test_capture(probe) -> void:
	var capture_id := "a".repeat(32)
	var response: Dictionary = probe._capture_game_view(capture_id, {
		"run_id": 10, "max_width": 320, "max_height": 180,
	})
	if DisplayServer.get_name() == "headless":
		assert(not response.ok)
		assert(response.error.code == ErrorEnvelope.UNSUPPORTED_CAPABILITY)
		return
	assert(response.ok)
	assert(response.result.capture_id == capture_id)
	assert(response.result.width <= 320)
	assert(response.result.height <= 180)
	assert(response.result.bytes > 8)
	var path := ProjectSettings.globalize_path(
		"res://.godot/godot_mcp/captures/%s.png" % capture_id,
	)
	assert(FileAccess.file_exists(path))
	assert(FileAccess.get_file_as_bytes(path).slice(0, 8) == PackedByteArray([
		137, 80, 78, 71, 13, 10, 26, 10,
	]))
	DirAccess.remove_absolute(path)


func _test_gameplay_service_contract() -> void:
	assert(RuntimeGameplayCommands != null)
	var source := FileAccess.get_file_as_string("res://addons/godot_mcp/runtime_probe.gd")
	assert("get_tree().root" in source)
	assert("CAPTURE_FOLDER" in source)
	assert("Input.action_press" in source)
	assert("Input.action_release" in source)
	assert("Expression.new" not in source)
	assert("eval(" not in source)
	assert("callv(" not in source)
