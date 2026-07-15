extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")

var _handlers: Dictionary = {}
var _owners: Dictionary = {}


func register_handler(
	command: String, handler: Callable, owner := "composition",
) -> Dictionary:
	return register_handlers(owner, {command: handler})


func register_handlers(owner: String, handlers: Dictionary) -> Dictionary:
	if owner.is_empty() or handlers.is_empty():
		return ErrorEnvelope.failure(
			"Command owner and handlers are required", ErrorEnvelope.INVALID_ARGUMENT,
		)
	for command_value in handlers:
		if not command_value is String or command_value.is_empty():
			return ErrorEnvelope.failure(
				"Command name is invalid", ErrorEnvelope.INVALID_ARGUMENT,
			)
		var command := command_value as String
		if _handlers.has(command):
			return ErrorEnvelope.failure(
				"Duplicate command ownership", ErrorEnvelope.INVALID_ARGUMENT,
				{
					"command": command,
					"owner": owner,
					"registered_owner": _owners[command],
				}, false,
			)
		var handler = handlers[command]
		if not handler is Callable or not (handler as Callable).is_valid():
			return ErrorEnvelope.failure(
				"Command handler is invalid", ErrorEnvelope.INVALID_ARGUMENT,
				{"command": command, "owner": owner}, false,
			)
	for command in handlers:
		_handlers[command] = handlers[command]
		_owners[command] = owner
	return ErrorEnvelope.success({"registered": handlers.size(), "owner": owner})


func commands() -> Array[String]:
	var output: Array[String] = []
	output.assign(_handlers.keys())
	output.sort()
	return output


func dispatch(command: String, arguments: Dictionary) -> Dictionary:
	if _handlers.has(command):
		return (_handlers[command] as Callable).call(arguments)
	return ErrorEnvelope.failure("Unknown command", ErrorEnvelope.INVALID_ARGUMENT)
