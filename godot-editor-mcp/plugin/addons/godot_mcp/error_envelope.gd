extends RefCounted

const MAX_DEPTH := 3
const MAX_ITEMS := 16
const MAX_TEXT := 512

const UNAUTHORIZED := "unauthorized"
const INVALID_ARGUMENT := "invalid_argument"
const PROTECTED_PATH := "protected_path"
const NOT_FOUND := "not_found"
const EDITOR_BUSY := "editor_busy"
const IMPORT_PENDING := "import_pending"
const NO_ACTIVE_RUN := "no_active_run"
const STALE_RUNTIME_ID := "stale_runtime_id"
const TIMEOUT := "timeout"
const UNSUPPORTED_CAPABILITY := "unsupported_capability"
const STALE_CURSOR := "stale_cursor"
const PROJECT_MISMATCH := "project_mismatch"
const SAVE_FAILED := "save_failed"
const MALFORMED_OPERATION := "malformed_operation"
const STALE_OPERATION := "stale_operation"
const VERSION_MISMATCH := "version_mismatch"
const RUNTIME_PROBE_UNAVAILABLE := "runtime_probe_unavailable"
const AMBIGUOUS_RUNTIME_SESSION := "ambiguous_runtime_session"


static func success(result: Variant) -> Dictionary:
	return {"ok": true, "result": result}


static func failure(
	message: String,
	code := "",
	details: Variant = {},
	retryable := false,
) -> Dictionary:
	var selected_code: String = code if not code.is_empty() else classify(message)
	return {
		"ok": false,
		"error": {
			"code": selected_code,
			"message": message.left(MAX_TEXT),
			"details": _bounded(details),
			"retryable": retryable,
		},
	}


static func classify(message: String) -> String:
	var lower := message.to_lower()
	if "unauthorized" in lower:
		return UNAUTHORIZED
	if "protected" in lower or "symbolic link" in lower:
		return PROTECTED_PATH
	if "not found" in lower or "does not exist" in lower or "no scene is open" in lower:
		return NOT_FOUND
	if "already running" in lower or "busy" in lower:
		return EDITOR_BUSY
	if "pending" in lower:
		return IMPORT_PENDING
	if "timeout" in lower or "timed out" in lower:
		return TIMEOUT
	if "unsupported" in lower or "unavailable" in lower:
		return UNSUPPORTED_CAPABILITY
	return INVALID_ARGUMENT


static func message(response: Dictionary) -> String:
	var error = response.get("error", {})
	if error is Dictionary:
		return str(error.get("message", "Godot command failed"))
	return str(error)


static func _bounded(value: Variant, depth := 0) -> Variant:
	if depth >= MAX_DEPTH:
		return "..."
	match typeof(value):
		TYPE_NIL, TYPE_BOOL, TYPE_INT, TYPE_FLOAT:
			return value
		TYPE_STRING, TYPE_STRING_NAME, TYPE_NODE_PATH:
			return str(value).left(MAX_TEXT)
		TYPE_ARRAY:
			var output: Array = []
			for item in value.slice(0, MAX_ITEMS):
				output.append(_bounded(item, depth + 1))
			return output
		TYPE_DICTIONARY:
			var output := {}
			for key in value.keys().slice(0, MAX_ITEMS):
				output[str(key).left(128)] = _bounded(value[key], depth + 1)
			return output
		_:
			return str(value).left(MAX_TEXT)
