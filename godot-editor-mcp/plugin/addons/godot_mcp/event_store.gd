extends RefCounted

const MAX_EVENTS := 128

var _next_id := 1
var _events: Array[Dictionary] = []


func append(category: String, details: Dictionary = {}, run_id: Variant = null) -> int:
	var event_id := _next_id
	_next_id += 1
	_events.append({
		"event_id": event_id,
		"time_unix_ms": int(Time.get_unix_time_from_system() * 1000.0),
		"category": category.left(64),
		"details": details,
		"run_id": run_id,
	})
	if _events.size() > MAX_EVENTS:
		_events.pop_front()
	return event_id


func latest_id() -> Variant:
	return null if _next_id == 1 else _next_id - 1
