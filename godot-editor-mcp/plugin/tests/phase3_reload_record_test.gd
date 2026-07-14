extends SceneTree

const ErrorEnvelope := preload("res://addons/godot_mcp/error_envelope.gd")
const ReloadCommands := preload("res://addons/godot_mcp/reload_commands.gd")


func _initialize() -> void:
	var now := 1_000_000
	var project_hash := "a".repeat(64)
	var valid := {
		"record_version": 1,
		"status": "pending",
		"operation_id": "op-123-1",
		"project_hash": project_hash,
		"bridge_version": "0.7.0",
		"created_unix_ms": now,
	}
	_check(
		ReloadCommands.validate_record(valid, project_hash, "0.7.0", now).ok,
		"valid pending record was rejected",
	)
	var malformed: Dictionary = valid.duplicate(true)
	malformed.erase("operation_id")
	_check_code(malformed, project_hash, "0.7.0", now, ErrorEnvelope.MALFORMED_OPERATION)
	var stale: Dictionary = valid.duplicate(true)
	stale.created_unix_ms = now - ReloadCommands.MAX_RECORD_AGE_MS - 1
	_check_code(stale, project_hash, "0.7.0", now, ErrorEnvelope.STALE_OPERATION)
	_check_code(valid, "b".repeat(64), "0.7.0", now, ErrorEnvelope.PROJECT_MISMATCH)
	_check_code(valid, project_hash, "0.8.0", now, ErrorEnvelope.VERSION_MISMATCH)
	print("Phase 3 reload record checks passed")
	quit(0)


func _check_code(
	record: Dictionary, project_hash: String, bridge_version: String,
	now: int, expected: String,
) -> void:
	var result: Dictionary = ReloadCommands.validate_record(
		record, project_hash, bridge_version, now,
	)
	_check(not result.ok and result.code == expected, "expected error code " + expected)


func _check(condition: bool, message: String) -> void:
	if condition:
		return
	push_error(message)
	quit(1)
