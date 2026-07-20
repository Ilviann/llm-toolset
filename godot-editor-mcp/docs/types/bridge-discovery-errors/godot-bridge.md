# Type: `GodotBridge`

**Source:** `godot_editor_mcp/bridge.py`

Concrete `BridgeClient` for one configured project. It reads the project token, selects an explicit or discovered localhost port, emits one bounded newline-delimited JSON request, receives one bounded response, and decodes editor failures.

The normal socket deadline is fixed and short; only the bounded runtime-condition path may extend it within its published maximum. The client validates configuration and response shape, never follows arbitrary hosts, and rereads credentials during reload reconnect through waiter-created calls.
