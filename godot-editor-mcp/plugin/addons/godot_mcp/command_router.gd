extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")

var _handlers: Dictionary = {}
var _services: Dictionary = {}


func register_handler(command: String, handler: Callable) -> void:
	_handlers[command] = handler


func register_service(commands: Array, service: RefCounted) -> void:
	for command in commands:
		_services[command] = service


func commands() -> Array[String]:
	var output: Array[String] = []
	output.assign(_handlers.keys() + _services.keys())
	output.sort()
	return output


func dispatch(command: String, arguments: Dictionary) -> Dictionary:
	if _handlers.has(command):
		return (_handlers[command] as Callable).call(arguments)
	if _services.has(command):
		return _services[command].execute(command, arguments)
	return ErrorEnvelope.failure("Unknown command", ErrorEnvelope.INVALID_ARGUMENT)
