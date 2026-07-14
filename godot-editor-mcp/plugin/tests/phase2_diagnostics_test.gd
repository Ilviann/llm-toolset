extends SceneTree

const DiagnosticStore := preload("res://addons/godot_mcp/diagnostic_store.gd")


func _initialize() -> void:
	var store = DiagnosticStore.new()
	var backtraces: Array[ScriptBacktrace] = []
	store._log_message("editor failure", true)
	store._log_error(
		"load_scene", "res://broken.tscn", 4, "", "scene load warning",
		false, Logger.ERROR_TYPE_WARNING, backtraces,
	)
	store.set_run_id(7)
	store._log_error(
		"parse", "res://bad.gd", 9, "", "Parse error: expected expression",
		false, Logger.ERROR_TYPE_ERROR, backtraces,
	)
	store._log_error(
		"tick", "res://main.tscn", 0, "", "runtime failure",
		false, Logger.ERROR_TYPE_ERROR, backtraces,
	)

	var parser: Dictionary = store.read({"scope": "parser", "severity": "error", "run_id": 7})
	_check(parser.ok and parser.result.diagnostics.size() == 1, "parser/run filtering failed")
	_check(parser.result.diagnostics[0].path == "res://bad.gd", "parser path was not retained")
	var warnings: Dictionary = store.read({"scope": "editor", "severity": "warning"})
	_check(warnings.ok and warnings.result.diagnostics.size() == 1, "scene-load warning filtering failed")
	var runtime: Dictionary = store.read({"scope": "runtime", "run_id": 7})
	_check(runtime.ok and runtime.result.diagnostics.size() == 1, "runtime filtering failed")

	store.set_run_id(null)
	for index in 260:
		store._log_message("bounded diagnostic %d" % index, true)
	var stale: Dictionary = store.read({"since": 0, "limit": 1})
	_check(not stale.ok and stale.error.code == "stale_cursor", "stale cursor was not separated")
	var snapshot: Dictionary = store.read({"limit": 100})
	_check(snapshot.ok and snapshot.result.diagnostics.size() == 100, "result limit was not enforced")
	print("Phase 2 diagnostic store checks passed")
	quit(0)


func _check(condition: bool, message: String) -> void:
	if condition:
		return
	push_error(message)
	quit(1)
