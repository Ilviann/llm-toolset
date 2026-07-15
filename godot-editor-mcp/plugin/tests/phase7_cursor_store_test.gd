extends SceneTree

const CursorStore := preload("../addons/godot_mcp/cursor_store.gd")


class Clock extends RefCounted:
	var now := 1000

	func read() -> int:
		return now


func _init() -> void:
	var clock := Clock.new()
	var cursors = CursorStore.new(Callable(clock, "read"))
	var snapshot := cursors.snapshot_id("assets", 4)
	assert(snapshot.length() == 64)
	var cursor: String = cursors.issue("assets", ["res://", "all", 2], snapshot, 2)
	assert(cursor.length() == 48)
	assert("secret" not in cursor)
	var prepared: Dictionary = cursors.prepare(
		cursor, "assets", ["res://", "all", 2],
	)
	assert(prepared.ok)
	assert(prepared.result.offset == 2)
	assert(prepared.result.snapshot == snapshot)
	var resumed: Dictionary = cursors.resume(
		cursor, "assets", ["res://", "all", 2], snapshot,
	)
	assert(resumed.ok and resumed.result == 2)

	var mismatch: Dictionary = cursors.resume(
		cursor, "assets", ["res://", "scene", 2], snapshot,
	)
	assert(not mismatch.ok and mismatch.error.code == "invalid_argument")
	var stale: Dictionary = cursors.resume(
		cursor, "assets", ["res://", "all", 2], cursors.snapshot_id("assets", 5),
	)
	assert(not stale.ok and stale.error.code == "stale_cursor")

	var expiring: String = cursors.issue("scene_tree", [".", 3, "", 50], "scene", 50)
	clock.now += 120001
	var expired: Dictionary = cursors.resume(
		expiring, "scene_tree", [".", 3, "", 50], "scene",
	)
	assert(not expired.ok and expired.error.code == "invalid_argument")
	var tampered: Dictionary = cursors.resume(
		expiring.left(47) + "f", "scene_tree", [".", 3, "", 50], "scene",
	)
	assert(not tampered.ok and tampered.error.code == "invalid_argument")

	print("phase7_cursor_store_test: PASS")
	quit()
