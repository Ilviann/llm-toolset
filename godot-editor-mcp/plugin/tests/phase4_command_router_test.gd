extends SceneTree

const CommandRouter := preload("res://addons/godot_mcp/command_router.gd")


func _initialize() -> void:
	var router = CommandRouter.new()
	var first: Dictionary = router.register_handlers("first", {
		"alpha": Callable(self, "_alpha"),
	})
	_check(first.ok, "initial handler registration failed")
	_check(router.commands() == ["alpha"], "registered command list is incorrect")
	var response: Dictionary = router.dispatch("alpha", {"value": 7})
	_check(response.ok and response.result.value == 7, "direct callable dispatch failed")

	var duplicate: Dictionary = router.register_handlers("second", {
		"alpha": Callable(self, "_alpha"),
		"beta": Callable(self, "_alpha"),
	})
	_check(not duplicate.ok, "duplicate ownership was accepted")
	_check(duplicate.error.details.registered_owner == "first", "owner was not reported")
	_check(router.commands() == ["alpha"], "failed registration was not atomic")

	var unknown: Dictionary = router.dispatch("missing", {})
	_check(not unknown.ok and unknown.error.code == "invalid_argument", "unknown command changed")
	print("Phase 4 command router checks passed")
	quit(0)


func _alpha(arguments: Dictionary) -> Dictionary:
	return {"ok": true, "result": arguments}


func _check(condition: bool, message: String) -> void:
	if condition:
		return
	push_error(message)
	quit(1)
