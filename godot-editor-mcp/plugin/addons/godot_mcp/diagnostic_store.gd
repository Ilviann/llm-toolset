extends Logger

const ErrorEnvelope := preload("error_envelope.gd")
const MAX_RECORDS := 256
const MAX_MESSAGE := 1024
const MAX_STACK_FRAMES := 8

var _mutex := Mutex.new()
var _records: Array[Dictionary] = []
var _next_id := 1
var _run_id: Variant = null
var _project_path := ""


func _init() -> void:
	_project_path = ProjectSettings.globalize_path("res://").trim_suffix("/")


func set_run_id(run_id: Variant) -> void:
	_mutex.lock()
	_run_id = run_id
	_mutex.unlock()


func _log_message(message: String, error: bool) -> void:
	# Normal print/progress output is not a diagnostic and may contain terminal
	# control sequences that do not belong in model-facing results.
	if not error:
		return
	_append_record(
		"error",
		"runtime" if _snapshot_run_id() != null else "editor",
		message,
	)


func _log_error(
	function: String,
	file: String,
	line: int,
	code: String,
	rationale: String,
	_editor_notify: bool,
	error_type: int,
	script_backtraces: Array[ScriptBacktrace],
) -> void:
	var message := rationale if not rationale.is_empty() else code
	var resource_path := _resource_path(file)
	var category := _category(resource_path, message)
	var severity := "warning" if error_type == ERROR_TYPE_WARNING else "error"
	var frames: Array[Dictionary] = []
	for backtrace in script_backtraces:
		if backtrace == null:
			continue
		for index in min(backtrace.get_frame_count(), MAX_STACK_FRAMES - frames.size()):
			frames.append({
				"path": _resource_path(backtrace.get_frame_file(index)),
				"line": backtrace.get_frame_line(index),
				"function": str(backtrace.get_frame_function(index)).left(128),
			})
		if frames.size() >= MAX_STACK_FRAMES:
			break
	_append_record(severity, category, message, resource_path, line, 0, function, frames)


func read(arguments: Dictionary) -> Dictionary:
	var scope = arguments.get("scope", "all")
	var severity = arguments.get("severity", "all")
	var has_cursor := arguments.has("since")
	var since = arguments.get("since", 0)
	var limit = arguments.get("limit", 50)
	var requested_run_id = arguments.get("run_id", null)
	if scope not in ["all", "parser", "runtime", "editor"]:
		return ErrorEnvelope.failure("Diagnostic scope is invalid")
	if severity not in ["all", "error", "warning"]:
		return ErrorEnvelope.failure("Diagnostic severity is invalid")
	if not (since is int or since is float) or float(since) != floor(float(since)) or since < 0:
		return ErrorEnvelope.failure("since must be a non-negative integer")
	if not (limit is int or limit is float) or float(limit) != floor(float(limit)) or limit < 1 or limit > 100:
		return ErrorEnvelope.failure("limit must be between 1 and 100")
	if requested_run_id != null and (
		not (requested_run_id is int or requested_run_id is float)
		or float(requested_run_id) != floor(float(requested_run_id))
		or requested_run_id < 1
	):
		return ErrorEnvelope.failure("run_id must be a positive integer")
	since = int(since)
	limit = int(limit)
	if requested_run_id != null:
		requested_run_id = int(requested_run_id)

	_mutex.lock()
	var snapshot := _records.duplicate(true)
	var latest := null if _next_id == 1 else _next_id - 1
	_mutex.unlock()
	if has_cursor and not snapshot.is_empty() and since < int(snapshot[0].event_id) - 1:
		return ErrorEnvelope.failure(
			"Diagnostic cursor is older than retained history",
			ErrorEnvelope.STALE_CURSOR,
			{"oldest_event_id": snapshot[0].event_id, "latest_event_id": latest},
		)
	var output: Array[Dictionary] = []
	for record in snapshot:
		if record.event_id <= since:
			continue
		if scope != "all" and record.category != scope:
			continue
		if severity != "all" and record.severity != severity:
			continue
		if requested_run_id != null and record.run_id != requested_run_id:
			continue
		output.append(record)
		if output.size() >= limit:
			break
	return ErrorEnvelope.success({
		"diagnostics": output,
		"latest_event_id": latest,
		"oldest_event_id": null if snapshot.is_empty() else snapshot[0].event_id,
		"truncated": output.size() >= limit,
	})


func latest_id() -> Variant:
	_mutex.lock()
	var value = null if _next_id == 1 else _next_id - 1
	_mutex.unlock()
	return value


func counts(run_id: Variant) -> Dictionary:
	_mutex.lock()
	var snapshot := _records.duplicate(false)
	_mutex.unlock()
	var output := {"errors": 0, "warnings": 0}
	for record in snapshot:
		if run_id != null and record.run_id != run_id:
			continue
		if run_id == null and record.run_id != null:
			continue
		if record.severity == "error":
			output.errors += 1
		elif record.severity == "warning":
			output.warnings += 1
	return output


func latest_error_for_path(path: String, since := 0) -> Variant:
	_mutex.lock()
	var snapshot := _records.duplicate(false)
	_mutex.unlock()
	for index in range(snapshot.size() - 1, -1, -1):
		var record: Dictionary = snapshot[index]
		if record.event_id > since and record.severity == "error" and record.path == path:
			return record
	return null


func _append_record(
	severity: String,
	category: String,
	message: String,
	path := "",
	line := 0,
	column := 0,
	function := "",
	stack_frames: Array[Dictionary] = [],
) -> void:
	_mutex.lock()
	var event_id := _next_id
	_next_id += 1
	_records.append({
		"event_id": event_id,
		"time_unix_ms": int(Time.get_unix_time_from_system() * 1000.0),
		"severity": severity,
		"category": category,
		"message": _clean_text(message, MAX_MESSAGE),
		"path": path,
		"line": maxi(line, 0),
		"column": maxi(column, 0),
		"function": _clean_text(function, 128),
		"stack_frames": stack_frames,
		"run_id": _run_id,
		"origin": null,
	})
	while _records.size() > MAX_RECORDS:
		_records.pop_front()
	_mutex.unlock()


func _snapshot_run_id() -> Variant:
	_mutex.lock()
	var value = _run_id
	_mutex.unlock()
	return value


func _category(path: String, message: String) -> String:
	if path.get_extension().to_lower() in ["gd", "cs"] or "parse error" in message.to_lower():
		return "parser"
	return "runtime" if _snapshot_run_id() != null else "editor"


func _resource_path(path: String) -> String:
	if path.begins_with("res://"):
		return path.left(512)
	var normalized := path.replace("\\", "/")
	var project := _project_path.replace("\\", "/")
	if normalized.begins_with(project + "/"):
		return ("res://" + normalized.trim_prefix(project + "/")).left(512)
	return ""


func _clean_text(value: String, limit: int) -> String:
	var output := ""
	for character in value:
		var code := character.unicode_at(0)
		if code < 32 or code == 127:
			if not output.ends_with(" "):
				output += " "
		else:
			output += character
		if output.length() >= limit:
			break
	return output.strip_edges().left(limit)
