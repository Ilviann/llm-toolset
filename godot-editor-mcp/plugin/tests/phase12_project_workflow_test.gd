extends SceneTree

const ErrorEnvelope := preload("../addons/godot_mcp/error_envelope.gd")
const ProjectPathGuard := preload("../addons/godot_mcp/project_path_guard.gd")
const ProjectWorkflowCommands := preload("../addons/godot_mcp/project_workflow_commands.gd")

const TEMP_NAME := "GodotMCPPhase12Temporary"
const TEMP_KEY := "autoload/" + TEMP_NAME
const TEST_PATH := "res://tests/fixtures/phase12_autoload.gd"

var _save_error := OK
var _marked_reload := false


func _init() -> void:
	if ProjectSettings.has_setting(TEMP_KEY):
		ProjectSettings.clear(TEMP_KEY)
	var service = ProjectWorkflowCommands.new(
		Callable(self, "_add_autoload"),
		Callable(self, "_remove_autoload"),
		Callable(self, "_mark_saved"),
		ProjectPathGuard.new(),
		Callable(self, "_save"),
	)
	var handlers: Dictionary = service.handlers()
	assert(handlers.keys() == ["list_autoloads", "autoload_patch", "list_editor_plugins"])

	var added: Dictionary = handlers.autoload_patch.call({
		"changes": [{
			"op": "add", "name": TEMP_NAME, "path": TEST_PATH, "expected": null,
		}],
		"save": false,
	})
	assert(added.ok)
	assert(added.result.diff[0].before == null)
	assert(added.result.diff[0].after.path == TEST_PATH)
	assert(added.result.requirements.project_reload)
	assert(ProjectSettings.get_setting(TEMP_KEY) == "*" + TEST_PATH)

	var listed: Dictionary = handlers.list_autoloads.call({})
	assert(listed.ok)
	var listed_entry := _find_named(listed.result.autoloads, TEMP_NAME)
	assert(listed_entry.path == TEST_PATH and listed_entry.value.path == TEST_PATH)

	var stale: Dictionary = handlers.autoload_patch.call({
		"changes": [{
			"op": "update", "name": TEMP_NAME, "path": TEST_PATH,
			"expected": "res://tests/phase11_scene_transaction_test.gd",
		}],
		"dry_run": true,
	})
	assert(not stale.ok and stale.error.code == ErrorEnvelope.INVALID_ARGUMENT)

	var protected: Dictionary = handlers.autoload_patch.call({
		"changes": [{
			"op": "remove", "name": "GodotMCPRuntimeProbe",
		}],
		"dry_run": true,
	})
	assert(not protected.ok and protected.error.code == ErrorEnvelope.PROTECTED_PATH)

	var invalid_path: Dictionary = handlers.autoload_patch.call({
		"changes": [{
			"op": "update", "name": TEMP_NAME, "path": "../outside.gd",
		}],
		"dry_run": true,
	})
	assert(not invalid_path.ok and invalid_path.error.code == ErrorEnvelope.INVALID_ARGUMENT)
	var conflicting_name: Dictionary = handlers.autoload_patch.call({
		"changes": [{"op": "add", "name": "Node", "path": TEST_PATH}],
		"dry_run": true,
	})
	assert(not conflicting_name.ok and conflicting_name.error.code == ErrorEnvelope.INVALID_ARGUMENT)
	var non_node_script: Dictionary = handlers.autoload_patch.call({
		"changes": [{
			"op": "update", "name": TEMP_NAME,
			"path": "res://tests/phase12_project_workflow_test.gd",
		}],
		"dry_run": true,
	})
	assert(not non_node_script.ok and non_node_script.error.code == ErrorEnvelope.INVALID_ARGUMENT)

	_save_error = ERR_CANT_CREATE
	var failed_save: Dictionary = handlers.autoload_patch.call({
		"changes": [{
			"op": "remove", "name": TEMP_NAME,
			"expected": {"path": TEST_PATH, "singleton": true},
		}],
	})
	assert(not failed_save.ok and failed_save.error.code == ErrorEnvelope.SAVE_FAILED)
	assert(ProjectSettings.get_setting(TEMP_KEY) == "*" + TEST_PATH)
	_save_error = OK

	var removed: Dictionary = handlers.autoload_patch.call({
		"changes": [{
			"op": "remove", "name": TEMP_NAME, "expected": TEST_PATH,
		}],
		"save": false,
	})
	assert(removed.ok and not ProjectSettings.has_setting(TEMP_KEY))
	assert(not _marked_reload)

	var plugins: Dictionary = handlers.list_editor_plugins.call({})
	assert(plugins.ok and plugins.result.enabled_setting_valid)
	var plugin: Dictionary = _find_path(
		plugins.result.plugins, "res://addons/godot_mcp/plugin.cfg",
	)
	assert(plugin.enabled and plugin.installed and plugin.valid)
	assert(plugin.name == "Godot MCP Bridge")
	assert(plugin.version == "0.17.0")

	if ProjectSettings.has_setting(TEMP_KEY):
		ProjectSettings.clear(TEMP_KEY)
	print("phase12_project_workflow_test: PASS")
	quit()


func _add_autoload(name: String, path: String) -> void:
	ProjectSettings.set_setting("autoload/" + name, "*" + path)


func _remove_autoload(name: String) -> void:
	ProjectSettings.clear("autoload/" + name)


func _mark_saved(needs_reload: bool) -> void:
	_marked_reload = needs_reload


func _save() -> int:
	return _save_error


func _find_named(records: Array, name: String) -> Dictionary:
	for record in records:
		if record.name == name:
			return record
	assert(false, "Autoload record not found")
	return {}


func _find_path(records: Array, path: String) -> Dictionary:
	for record in records:
		if record.path == path:
			return record
	assert(false, "Plugin record not found")
	return {}
