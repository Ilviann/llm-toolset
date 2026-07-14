extends RefCounted

const MAX_OPERATIONS := 64

var _next_id := 1
var _operations: Dictionary = {}
var _order: Array[String] = []


func accept(kind: String, details: Dictionary = {}, run_id: Variant = null) -> String:
	var operation_id := "op-%d-%d" % [OS.get_process_id(), _next_id]
	_next_id += 1
	_operations[operation_id] = {
		"operation_id": operation_id,
		"kind": kind.left(64),
		"status": "accepted",
		"run_id": run_id,
		"details": details,
	}
	_order.append(operation_id)
	while _order.size() > MAX_OPERATIONS:
		_operations.erase(_order.pop_front())
	return operation_id


func complete(operation_id: String, details: Dictionary = {}) -> void:
	if not _operations.has(operation_id):
		return
	_operations[operation_id]["status"] = "completed"
	_operations[operation_id]["completion"] = details


func complete_kind(kind: String, details: Dictionary = {}) -> void:
	for operation_id in _order:
		var operation: Dictionary = _operations[operation_id]
		if operation.kind == kind and operation.status == "accepted":
			complete(operation_id, details)


func concise_active(limit := 16) -> Array[Dictionary]:
	var output: Array[Dictionary] = []
	for operation_id in _order:
		var operation: Dictionary = _operations[operation_id]
		if operation.status == "accepted":
			output.append({
				"operation_id": operation.operation_id,
				"kind": operation.kind,
				"run_id": operation.run_id,
			})
			if output.size() >= limit:
				break
	return output


func concise_recent(kind := "", limit := 16) -> Array[Dictionary]:
	var output: Array[Dictionary] = []
	for index in range(_order.size() - 1, -1, -1):
		var operation: Dictionary = _operations[_order[index]]
		if operation.status != "completed" or (not kind.is_empty() and operation.kind != kind):
			continue
		output.append({
			"operation_id": operation.operation_id,
			"kind": operation.kind,
			"run_id": operation.run_id,
			"details": operation.details,
			"completion": operation.get("completion", {}),
		})
		if output.size() >= limit:
			break
	return output
