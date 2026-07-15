extends SceneTree

const EditorStateMonitor := preload("../addons/godot_mcp/editor_state_monitor.gd")
const EventStore := preload("../addons/godot_mcp/event_store.gd")
const ImportStateTracker := preload("../addons/godot_mcp/import_state_tracker.gd")
const OperationRegistry := preload("../addons/godot_mcp/operation_registry.gd")
const ProjectFileStateTracker := preload("../addons/godot_mcp/project_file_state_tracker.gd")
const RunStateTracker := preload("../addons/godot_mcp/run_state_tracker.gd")
const SceneStateTracker := preload("../addons/godot_mcp/scene_state_tracker.gd")


class FakeHistory extends RefCounted:
	var version := 0

	func get_version() -> int:
		return version


class FakeUndoRedo extends RefCounted:
	var history := FakeHistory.new()
	func get_object_history_id(_object: Object) -> int:
		return 1

	func get_history_undo_redo(_instance_id: int):
		return history


class FakeSelection extends RefCounted:
	var nodes: Array[Node] = []

	func get_selected_nodes() -> Array[Node]:
		return nodes


class FakeFilesystem extends RefCounted:
	signal filesystem_changed
	signal resources_reimported(paths: PackedStringArray)

	var scanning := false
	var progress := 0.0
	var file_types: Dictionary = {}

	func is_scanning() -> bool:
		return scanning

	func get_scanning_progress() -> float:
		return progress

	func get_file_type(path: String) -> String:
		return str(file_types.get(path, ""))


class FakeDiagnostics extends RefCounted:
	var run_id: Variant = null
	var latest := 0
	var path_error: Variant = null

	func set_run_id(value: Variant) -> void:
		run_id = value

	func counts(_value: int) -> Dictionary:
		return {"errors": 0, "warnings": 0}

	func latest_id() -> Variant:
		return null if latest == 0 else latest

	func latest_error_for_path(_path: String, _since: int) -> Variant:
		return path_error


class FakeEditor extends RefCounted:
	var root: Node
	var selection := FakeSelection.new()
	var filesystem := FakeFilesystem.new()
	var playing := false

	func get_edited_scene_root() -> Node:
		return root

	func get_selection() -> FakeSelection:
		return selection

	func get_resource_filesystem() -> FakeFilesystem:
		return filesystem

	func is_playing_scene() -> bool:
		return playing

	func save_scene() -> void:
		pass

	func play_current_scene() -> void:
		playing = true

	func stop_playing_scene() -> void:
		playing = false


class HashSource extends RefCounted:
	var value := "a"

	func read() -> String:
		return value


func _init() -> void:
	var events = EventStore.new()
	var operations = OperationRegistry.new()
	var diagnostics = FakeDiagnostics.new()
	var editor = FakeEditor.new()
	var undo = FakeUndoRedo.new()
	editor.root = _scene_root("res://one.tscn", "One")
	editor.selection.nodes.append(editor.root)

	var scene = SceneStateTracker.new(editor, undo, events, operations)
	undo.history.version = 1
	assert(scene.state().scene_dirty)
	scene.mark_saved()
	assert(not scene.state().scene_dirty)
	var open_operation: String = operations.accept("open_scene", {"path": "res://two.tscn"})
	editor.root.free()
	editor.root = _scene_root("res://two.tscn", "Two")
	scene.poll()
	assert(scene.state().scene_event_id != null)
	assert(operations.concise_active().all(func(item): return item.operation_id != open_operation))

	var run = RunStateTracker.new(editor, events, operations, diagnostics)
	var started: Dictionary = run.run()
	assert(started.ok and started.result.run_id == 1)
	run.poll()
	assert(run.state().playing and run.state().run_event_id != null)
	var stopped: Dictionary = run.stop_run({"run_id": 1})
	assert(stopped.ok)
	run.poll()
	assert(not run.state().playing)
	assert(run.state().last_stop_reason == "requested")

	editor.filesystem.scanning = true
	var imports = ImportStateTracker.new(editor, events, operations, diagnostics)
	var import_operation: String = operations.accept("filesystem_scan", {"path": "res://data.csv"})
	imports.track_import("res://data.csv", import_operation)
	editor.filesystem.scanning = false
	editor.filesystem.filesystem_changed.emit()
	var import_state: Dictionary = imports.state()
	assert(import_state.filesystem_generation == 1)
	assert(import_state.filesystem_event_id != null)
	assert(import_state.recent_imports.size() == 1)
	assert(import_state.recent_imports[0].status == "completed")
	var failed_operation: String = operations.accept(
		"filesystem_scan", {"path": "res://broken.png"},
	)
	diagnostics.path_error = {"message": "decode failed", "event_id": 9}
	imports.track_import("res://broken.png", failed_operation)
	editor.filesystem.filesystem_changed.emit()
	import_state = imports.state()
	assert(import_state.recent_imports[0].status == "failed")
	assert(import_state.import_errors.size() == 1)
	diagnostics.path_error = null

	var hash_source = HashSource.new()
	var project_file = ProjectFileStateTracker.new(Callable(hash_source, "read"))
	hash_source.value = "b"
	assert(project_file.state().project_reload_required)
	project_file.mark_saved(false)
	assert(project_file.state().project_file_hash_matches_known_write)

	var monitor = EditorStateMonitor.new(
		events, operations, diagnostics, scene, run, imports, project_file,
	)
	var aggregate: Dictionary = monitor.state()
	var expected_fields := [
		"godot", "project_name", "project_path", "main_scene", "scene", "root",
		"scene_dirty", "selected", "playing", "filesystem_scanning",
		"filesystem_phase", "filesystem_progress", "filesystem_generation",
		"active_imports", "recent_imports", "import_errors", "run_id", "last_run_id",
		"last_run_exit_status", "last_stop_reason", "current_run_diagnostic_counts",
		"project_file_hash", "project_file_hash_matches_known_write",
		"project_reload_required", "last_event_id", "last_diagnostic_id",
		"filesystem_event_id", "scene_event_id", "run_event_id", "active_operations",
	]
	assert(aggregate.size() == expected_fields.size())
	for field in expected_fields:
		assert(aggregate.has(field))
	assert(aggregate.active_operations.size() <= 16)
	assert(aggregate.recent_imports.size() <= 16)

	monitor.stop()
	editor.root.free()
	print("phase6_state_trackers_test: PASS")
	quit()


func _scene_root(path: String, node_name: String) -> Node:
	var root := Node.new()
	root.name = node_name
	root.scene_file_path = path
	return root
