# Operation-waiting contracts

Use the index to retrieve only the contract section relevant to the task.

## Types: state payload views

**Source:** `godot_editor_mcp/state_payloads.py`

- `ImportPayload` validates project-relative path, operation identity, status, and failure fields for one import record.
- `EditorStatePayload` exposes validated scene, filesystem/import, operation, run, diagnostic, project, bridge, and version fields used by wait predicates.
- `ReloadStatusPayload` validates reload state plus project hash, bridge version, operation ID, completion/recovery fields, and cross-field consistency.

These immutable views validate fields lazily when accessed and tolerate unrelated extra fields for forward-compatible observation. A predicate must access every field on which success depends; missing, wrong-type, or inconsistent values become `InvalidResponseError`.

## Type: `OperationWaiter`

**Source:** `godot_editor_mcp/waiting.py`

Coordinator constructed with a bridge-like client, monotonic clock/sleeper, and cancellation event. Focused methods wait for scene open, scan/import, run startup, stop, or project reload.

Each method begins from an accepted result carrying an operation/run identity, polls validated payload views, shares one deadline, raises stable timeout/cancellation/protocol failures, and returns a concise completion result. Reload is special: it tolerates the expected disconnect, rediscovers and reauthenticates, then verifies exact project/version/operation identity.

## Library: monotonic deadlines

**Source:** private `_Deadline` in `godot_editor_mcp/waiting.py`

Immutable helper holding one absolute monotonic end time. It reports remaining time and expiration so polling, reconnect, diagnostic settling, and sleep intervals consume the same caller-provided timeout.

Never reset the deadline after progress or reconnect. Wall-clock time is unsuitable because system clock changes could extend or prematurely end waits. Tests inject deterministic clocks rather than sleeping.
