# Type: `OperationWaiter`

**Source:** `godot_editor_mcp/waiting.py`

Coordinator constructed with a bridge-like client, monotonic clock/sleeper, and cancellation event. Focused methods wait for scene open, scan/import, run startup, stop, or project reload.

Each method begins from an accepted result carrying an operation/run identity, polls validated payload views, shares one deadline, raises stable timeout/cancellation/protocol failures, and returns a concise completion result. Reload is special: it tolerates the expected disconnect, rediscovers and reauthenticates, then verifies exact project/version/operation identity.
