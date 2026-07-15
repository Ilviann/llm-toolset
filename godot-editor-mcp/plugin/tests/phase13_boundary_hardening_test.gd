extends SceneTree

const BridgeServer := preload("../addons/godot_mcp/bridge_server.gd")
const AuthenticatedStartup := preload("../addons/godot_mcp/authenticated_startup.gd")
const ErrorEnvelope := preload("../addons/godot_mcp/error_envelope.gd")
const Limits := preload("../addons/godot_mcp/command_limits.gd")
const TokenStore := preload("../addons/godot_mcp/token_store.gd")

const TOKEN := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"

var _now := 0
var _handled := 0
var _pending_response: Variant = null
var _listener_starts := 0
var _discovery_starts := 0


class FakePeer extends RefCounted:
	var incoming := PackedByteArray()
	var outgoing := PackedByteArray()
	var disconnected := false

	func _init(text := "") -> void:
		incoming = str(text).to_utf8_buffer()

	func poll() -> void:
		pass

	func get_status() -> int:
		return StreamPeerTCP.STATUS_NONE if disconnected else StreamPeerTCP.STATUS_CONNECTED

	func get_available_bytes() -> int:
		return incoming.size()

	func get_data(count: int) -> Array:
		var data := incoming.slice(0, count)
		incoming = incoming.slice(count)
		return [OK, data]

	func put_data(data: PackedByteArray) -> int:
		outgoing.append_array(data)
		return OK

	func disconnect_from_host() -> void:
		disconnected = true


class FakeServer extends RefCounted:
	var queued: Array = []
	var stopped := false

	func listen(_port: int, _host: String) -> int:
		return OK

	func stop() -> void:
		stopped = true

	func is_connection_available() -> bool:
		return not queued.is_empty()

	func take_connection():
		return queued.pop_front()


func _initialize() -> void:
	_test_normal_idle_incomplete_and_deferred_clients()
	_test_excess_connections_and_shutdown_cleanup()
	_test_token_failures_abort_startup()
	print("phase13_boundary_hardening_test: PASS")
	quit(0)


func _test_normal_idle_incomplete_and_deferred_clients() -> void:
	_now = 0
	_handled = 0
	_pending_response = null
	var fake_server := FakeServer.new()
	var bridge = BridgeServer.new(fake_server, Callable(self, "_clock"))
	assert(bridge.start(
		6505, TOKEN, Callable(self, "_handle"), Callable(self, "_resolve_pending"),
	) == OK)

	var normal := FakePeer.new(_request("state", {}))
	fake_server.queued.append(normal)
	bridge.poll()
	assert(normal.disconnected)
	assert(_handled == 1)
	var normal_response = JSON.parse_string(normal.outgoing.get_string_from_utf8())
	assert(normal_response.ok and normal_response.result.command == "state")

	var idle := FakePeer.new()
	fake_server.queued.append(idle)
	bridge.poll()
	assert(not idle.disconnected)
	_now = Limits.BRIDGE_CLIENT_TIMEOUT_MSEC
	bridge.poll()
	assert(idle.disconnected)

	_now = 0
	var incomplete := FakePeer.new('{"token":"%s"' % TOKEN)
	fake_server.queued.append(incomplete)
	bridge.poll()
	assert(not incomplete.disconnected)
	_now = Limits.BRIDGE_CLIENT_TIMEOUT_MSEC
	bridge.poll()
	assert(incomplete.disconnected)

	_now = 0
	var deferred := FakePeer.new(_request("defer", {}))
	fake_server.queued.append(deferred)
	bridge.poll()
	assert(not deferred.disconnected)
	bridge.poll()
	assert(not deferred.disconnected)
	_pending_response = ErrorEnvelope.success({"completed": true})
	bridge.poll()
	assert(deferred.disconnected)
	var deferred_response = JSON.parse_string(deferred.outgoing.get_string_from_utf8())
	assert(deferred_response.ok and deferred_response.result.completed)


func _test_excess_connections_and_shutdown_cleanup() -> void:
	_now = 0
	var fake_server := FakeServer.new()
	var peers: Array = []
	for index in range(Limits.MAX_BRIDGE_CLIENTS + 1):
		var peer := FakePeer.new()
		peers.append(peer)
		fake_server.queued.append(peer)
	var bridge = BridgeServer.new(fake_server, Callable(self, "_clock"))
	bridge.start(6505, TOKEN, Callable(self, "_handle"))
	bridge.poll()
	assert(bridge.get("_clients").size() == Limits.MAX_BRIDGE_CLIENTS)
	assert(peers.back().disconnected)
	bridge.stop()
	assert(fake_server.stopped)
	assert(bridge.get("_clients").is_empty())
	assert(peers.all(func(peer): return peer.disconnected))


func _test_token_failures_abort_startup() -> void:
	var unreadable = TokenStore.new(
		func(_path): return {"ok": false, "exists": true, "text": ""},
		Callable(), func(): return TOKEN,
	)
	var read_result: Dictionary = unreadable.load_or_create("res://token")
	assert(not read_result.ok and read_result.error.code == ErrorEnvelope.SAVE_FAILED)

	var unwritable = TokenStore.new(
		func(_path): return {"ok": true, "exists": false, "text": ""},
		func(_path, _token): return ERR_FILE_CANT_WRITE,
		func(): return TOKEN,
	)
	var write_result: Dictionary = unwritable.load_or_create("res://token")
	assert(not write_result.ok and write_result.error.code == ErrorEnvelope.SAVE_FAILED)

	_listener_starts = 0
	_discovery_starts = 0
	var startup_result: Dictionary = AuthenticatedStartup.new().run(
		write_result, Callable(self, "_start_fake_planes"),
	)
	assert(not startup_result.ok)
	assert(_listener_starts == 0)
	assert(_discovery_starts == 0)


func _request(command: String, arguments: Dictionary) -> String:
	return JSON.stringify({
		"token": TOKEN, "command": command, "arguments": arguments,
	}) + "\n"


func _handle(command: String, _arguments: Dictionary) -> Dictionary:
	_handled += 1
	if command == "defer":
		return {"__godot_mcp_deferred_response": "pending-1"}
	return ErrorEnvelope.success({"command": command})


func _resolve_pending(_pending_id: String) -> Variant:
	return _pending_response


func _clock() -> int:
	return _now


func _start_fake_planes(_token: String) -> Dictionary:
	_listener_starts += 1
	_discovery_starts += 1
	return ErrorEnvelope.success({})
