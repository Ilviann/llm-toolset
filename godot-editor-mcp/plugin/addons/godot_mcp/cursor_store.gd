extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")
const Limits := preload("command_limits.gd")

var _records: Dictionary = {}
var _clock: Callable


func _init(clock := Callable()) -> void:
	_clock = clock


func snapshot_id(scope: String, identity: Variant) -> String:
	return (scope + "\n" + JSON.stringify(identity)).sha256_text()


func issue(kind: String, query: Variant, snapshot: String, offset: int) -> String:
	_prune_expired()
	while _records.size() >= Limits.MAX_ACTIVE_CURSORS:
		_evict_oldest()
	var cursor := Crypto.new().generate_random_bytes(int(Limits.MAX_CURSOR_CHARS / 2)).hex_encode()
	while _records.has(cursor):
		cursor = Crypto.new().generate_random_bytes(int(Limits.MAX_CURSOR_CHARS / 2)).hex_encode()
	_records[cursor] = {
		"kind": kind,
		"query": _fingerprint(query),
		"snapshot": snapshot,
		"offset": offset,
		"issued_msec": _now_msec(),
		"expires_msec": _now_msec() + Limits.CURSOR_TTL_MSEC,
	}
	return cursor


func resume(cursor_value: Variant, kind: String, query: Variant, snapshot: String) -> Dictionary:
	if not cursor_value is String or cursor_value.length() != Limits.MAX_CURSOR_CHARS or not cursor_value.is_valid_hex_number():
		return ErrorEnvelope.failure(
			"Cursor is malformed", ErrorEnvelope.INVALID_ARGUMENT,
		)
	if not _records.has(cursor_value):
		return ErrorEnvelope.failure(
			"Cursor is unknown or expired", ErrorEnvelope.INVALID_ARGUMENT,
		)
	var record: Dictionary = _records[cursor_value]
	if int(record.expires_msec) <= _now_msec():
		_records.erase(cursor_value)
		return ErrorEnvelope.failure(
			"Cursor has expired", ErrorEnvelope.INVALID_ARGUMENT,
		)
	if record.kind != kind or record.query != _fingerprint(query):
		return ErrorEnvelope.failure(
			"Cursor does not match this query", ErrorEnvelope.INVALID_ARGUMENT,
		)
	if record.snapshot != snapshot:
		_records.erase(cursor_value)
		return ErrorEnvelope.failure(
			"Cursor snapshot is stale", ErrorEnvelope.STALE_CURSOR,
			{"expected_snapshot": record.snapshot, "current_snapshot": snapshot},
		)
	return ErrorEnvelope.success(int(record.offset))


func clear() -> void:
	_records.clear()


func _fingerprint(value: Variant) -> String:
	return JSON.stringify(value).sha256_text()


func _now_msec() -> int:
	if _clock.is_valid():
		return int(_clock.call())
	return int(Time.get_ticks_msec())


func _prune_expired() -> void:
	var now := _now_msec()
	for cursor in _records.keys():
		if int(_records[cursor].expires_msec) <= now:
			_records.erase(cursor)


func _evict_oldest() -> void:
	var oldest_cursor := ""
	var oldest_msec := 0
	for cursor in _records:
		var issued_msec := int(_records[cursor].issued_msec)
		if oldest_cursor.is_empty() or issued_msec < oldest_msec:
			oldest_cursor = cursor
			oldest_msec = issued_msec
	if not oldest_cursor.is_empty():
		_records.erase(oldest_cursor)
