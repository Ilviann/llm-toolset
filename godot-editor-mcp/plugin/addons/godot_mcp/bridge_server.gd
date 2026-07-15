extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")
const Limits := preload("command_limits.gd")

const HOST := "127.0.0.1"

var _server
var _clock: Callable
var _clients: Array[Dictionary] = []
var _token := ""
var _handler: Callable
var _pending_resolver: Callable


func _init(server = null, clock: Callable = Callable()) -> void:
	_server = server if server != null else TCPServer.new()
	_clock = clock


func start(
	port: int, token: String, handler: Callable, pending_resolver := Callable(),
) -> int:
	_token = token
	_handler = handler
	_pending_resolver = pending_resolver
	return _server.listen(port, HOST)


func stop() -> void:
	for client in _clients:
		client.peer.disconnect_from_host()
	_clients.clear()
	_server.stop()


func poll() -> void:
	while _server.is_connection_available():
		var peer: Variant = _server.take_connection()
		if peer == null:
			continue
		if _clients.size() >= Limits.MAX_BRIDGE_CLIENTS:
			peer.disconnect_from_host()
			continue
		_clients.append({
			"peer": peer,
			"buffer": PackedByteArray(),
			"deadline_msec": _now_msec() + Limits.BRIDGE_CLIENT_TIMEOUT_MSEC,
		})

	for index in range(_clients.size() - 1, -1, -1):
		var client := _clients[index]
		var peer = client.peer
		peer.poll()
		if peer.get_status() != StreamPeerTCP.STATUS_CONNECTED:
			_clients.remove_at(index)
			continue
		if client.has("pending_response_id"):
			var pending_response: Variant = null
			if _pending_resolver.is_valid():
				pending_response = _pending_resolver.call(client.pending_response_id)
			if pending_response is Dictionary:
				_send(peer, pending_response)
				_clients.remove_at(index)
			continue
		if _now_msec() >= int(client.deadline_msec):
			peer.disconnect_from_host()
			_clients.remove_at(index)
			continue
		var available: int = int(peer.get_available_bytes())
		if available <= 0:
			continue
		var received: Array = peer.get_data(available)
		if received[0] != OK:
			peer.disconnect_from_host()
			_clients.remove_at(index)
			continue
		var buffer := client.buffer as PackedByteArray
		buffer.append_array(received[1])
		if buffer.size() > Limits.MAX_REQUEST_BYTES:
			_send(peer, ErrorEnvelope.failure(
				"Request is too large", "request_too_large",
				{"limit": Limits.MAX_REQUEST_BYTES}, false,
			))
			_clients.remove_at(index)
			continue
		var newline := buffer.find(10)
		if newline < 0:
			client.buffer = buffer
			continue
		var line := buffer.slice(0, newline).get_string_from_utf8()
		var response := _handle_line(line)
		if response.has("__godot_mcp_deferred_response"):
			client["pending_response_id"] = response.__godot_mcp_deferred_response
			client.erase("buffer")
			client.erase("deadline_msec")
			continue
		_send(peer, response)
		_clients.remove_at(index)


func _handle_line(line: String) -> Dictionary:
	var request = JSON.parse_string(line)
	if not request is Dictionary:
		return ErrorEnvelope.failure("Invalid request", ErrorEnvelope.INVALID_ARGUMENT)
	if not _constant_time_equal(str(request.get("token", "")), _token):
		return ErrorEnvelope.failure("Unauthorized", ErrorEnvelope.UNAUTHORIZED)
	var command = request.get("command")
	var arguments = request.get("arguments", {})
	if not command is String or not arguments is Dictionary:
		return ErrorEnvelope.failure("Invalid request", ErrorEnvelope.INVALID_ARGUMENT)
	return _handler.call(command, arguments)


func _send(peer, response: Dictionary) -> void:
	var encoded := (JSON.stringify(response) + "\n").to_utf8_buffer()
	if encoded.size() > Limits.MAX_RESPONSE_BYTES:
		encoded = (JSON.stringify(ErrorEnvelope.failure(
			"Response is too large", "response_too_large",
			{"limit": Limits.MAX_RESPONSE_BYTES}, false,
		)) + "\n").to_utf8_buffer()
	peer.put_data(encoded)
	peer.disconnect_from_host()


func _now_msec() -> int:
	return int(_clock.call()) if _clock.is_valid() else int(Time.get_ticks_msec())


func _constant_time_equal(left: String, right: String) -> bool:
	var different := left.length() ^ right.length()
	for index in range(max(left.length(), right.length())):
		var a := left.unicode_at(index) if index < left.length() else 0
		var b := right.unicode_at(index) if index < right.length() else 0
		different |= a ^ b
	return different == 0
