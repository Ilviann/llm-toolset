extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")
const Limits := preload("command_limits.gd")

var _context
var _pending: Dictionary = {}


func _init(context) -> void:
	_context = context


func begin(request_id: String, arguments: Dictionary) -> Dictionary:
	var validation := validate_condition(arguments)
	if not validation.ok:
		return {"pending": false, "response": validation}
	var evaluation := evaluate_condition(arguments)
	if not evaluation.ok or bool(evaluation.result.get("matched", false)):
		return {"pending": false, "response": evaluation}
	_pending[request_id] = {
		"arguments": arguments.duplicate(true),
		"deadline_msec": int(Time.get_ticks_msec()) + int(arguments.get("timeout_ms", 1000)),
		"last_evidence": evaluation.result.get("evidence", {}),
	}
	return {"pending": true}


func poll(send_response: Callable) -> void:
	var now := int(Time.get_ticks_msec())
	for request_id in _pending.keys():
		var pending: Dictionary = _pending[request_id]
		var evaluation := evaluate_condition(pending.arguments)
		if not evaluation.ok or bool(evaluation.result.get("matched", false)):
			_pending.erase(request_id)
			send_response.call(request_id, evaluation)
			continue
		pending.last_evidence = evaluation.result.get("evidence", {})
		if now >= int(pending.deadline_msec):
			_pending.erase(request_id)
			send_response.call(request_id, ErrorEnvelope.failure(
				"Runtime condition timed out", ErrorEnvelope.TIMEOUT,
				{"condition": pending.arguments.get("condition"), "evidence": pending.last_evidence},
				true,
			))


func clear() -> void:
	_pending.clear()


func validate_condition(arguments: Dictionary) -> Dictionary:
	var run_check: Dictionary = _context.requested_run(arguments)
	if not run_check.ok:
		return run_check
	if arguments.get("scope") != "runtime":
		return _context.failure("Runtime condition scope must be runtime")
	var condition = arguments.get("condition")
	if condition not in ["node_exists", "node_count", "property"]:
		return _context.failure("Runtime condition type is unsupported")
	var timeout_result: Dictionary = _context.bounded_integer(
		arguments.get("timeout_ms", 1000), "Condition timeout", 1,
		Limits.MAX_CONDITION_TIMEOUT_MSEC,
	)
	if not timeout_result.ok:
		return timeout_result
	var path_result: Dictionary = _context.validate_path(arguments.get("path", "."))
	if not path_result.ok:
		return path_result
	if condition == "node_exists":
		if not arguments.get("exists", true) is bool:
			return _context.failure("exists must be a boolean")
		return _context.success({})
	if condition == "node_count":
		var depth_result: Dictionary = _context.bounded_integer(
			arguments.get("max_depth", Limits.MAX_TREE_DEPTH), "Maximum depth", 0,
			Limits.MAX_TREE_DEPTH,
		)
		if not depth_result.ok:
			return depth_result
		if arguments.has("group"):
			var group = arguments.group
			if not group is String or group.is_empty() or group.length() > 128:
				return _context.failure("Group must be a string up to 128 characters")
		var count_value = arguments.get("value")
		if (
			(not count_value is int and not count_value is float)
			or float(count_value) != floorf(float(count_value))
			or int(count_value) < 0
		):
			return _context.failure("Node-count value must be a non-negative integer")
		arguments["value"] = int(count_value)
	else:
		var property_name = arguments.get("property")
		if not property_name is String or property_name.is_empty() or property_name.length() > 128:
			return _context.failure("Property condition requires a bounded property name")
		if not arguments.has("value") or not _is_scalar(arguments.value):
			return _context.failure("Property comparison value must be a bounded scalar")
	var comparison = arguments.get("comparison", "eq")
	if comparison not in ["eq", "ne", "lt", "lte", "gt", "gte"]:
		return _context.failure("Comparison is unsupported")
	return _context.success({})


func evaluate_condition(arguments: Dictionary) -> Dictionary:
	var condition: String = arguments.condition
	if condition == "node_exists":
		var node: Node = _context.condition_node(arguments.path if arguments.has("path") else ".")
		var actual := node != null
		var expected := bool(arguments.get("exists", true))
		return _condition_result(condition, actual == expected, {
			"path": arguments.get("path", "."), "exists": actual,
		})
	var resolved: Dictionary = _context.resolve_node(arguments.get("path", "."))
	if not resolved.ok:
		return resolved
	var target := resolved.result as Node
	if condition == "node_count":
		var state := {"visited": 0, "count": 0, "paths": []}
		_count_condition_nodes(
			_context.ignored_node.get_tree().current_scene, target, 0,
			int(arguments.get("max_depth", Limits.MAX_TREE_DEPTH)),
			str(arguments.get("group", "")), state,
		)
		var matched = _compare_values(
			state.count, arguments.value, str(arguments.get("comparison", "eq")),
		)
		return _condition_result(condition, matched, {
			"path": arguments.get("path", "."), "group": arguments.get("group"),
			"count": state.count, "visited": state.visited, "paths": state.paths,
		})
	var property_name: String = arguments.property
	if not _is_builtin_property(target, property_name):
		return ErrorEnvelope.failure(
			"Runtime conditions can read only built-in Godot properties",
			ErrorEnvelope.UNSUPPORTED_CAPABILITY, {"property": property_name}, false,
		)
	var actual_value = target.get(property_name)
	if not _is_scalar(actual_value):
		return ErrorEnvelope.failure(
			"Runtime property is not a bounded scalar",
			ErrorEnvelope.UNSUPPORTED_CAPABILITY, {"property": property_name}, false,
		)
	var comparison: String = arguments.get("comparison", "eq")
	var compared = _compare_values(actual_value, arguments.value, comparison)
	if compared == null:
		return _context.failure("Property values do not support the requested comparison")
	return _condition_result(condition, bool(compared), {
		"path": arguments.get("path", "."), "property": property_name,
		"actual": actual_value, "comparison": comparison,
	})


func _condition_result(condition: String, matched: bool, evidence: Dictionary) -> Dictionary:
	return _context.success({
		"scope": "runtime", "run_id": _context.run_id, "condition": condition,
		"matched": matched, "evidence": evidence,
	})


func _count_condition_nodes(
	root: Node, node: Node, depth: int, max_depth: int, group: String, state: Dictionary,
) -> void:
	if state.visited >= Limits.MAX_TREE_SCAN:
		return
	state.visited += 1
	if group.is_empty() or node.is_in_group(StringName(group)):
		state.count += 1
		if state.paths.size() < Limits.MAX_CONDITION_EVIDENCE:
			state.paths.append(_context.node_path(root, node))
	if depth >= max_depth:
		return
	for child in node.get_children():
		if child != _context.ignored_node:
			_count_condition_nodes(root, child, depth + 1, max_depth, group, state)


func _compare_values(actual: Variant, expected: Variant, comparison: String) -> Variant:
	if comparison == "eq":
		return actual == expected
	if comparison == "ne":
		return actual != expected
	var both_numbers := (
		(actual is int or actual is float) and (expected is int or expected is float)
	)
	var both_strings := actual is String and expected is String
	if not both_numbers and not both_strings:
		return null
	match comparison:
		"lt": return actual < expected
		"lte": return actual <= expected
		"gt": return actual > expected
		"gte": return actual >= expected
	return null


func _is_builtin_property(node: Node, property_name: String) -> bool:
	for info in ClassDB.class_get_property_list(node.get_class()):
		if str(info.get("name", "")) == property_name:
			return true
	return false


func _is_scalar(value: Variant) -> bool:
	if value is String:
		return value.length() <= 512
	if value is float:
		return is_finite(value)
	return value == null or value is bool or value is int
