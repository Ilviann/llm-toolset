# Runtime debugger/validation contracts

Use the index to retrieve only the contract section relevant to the task.

## Type: runtime identity envelope

**Sources:** `runtime_debugger_gateway.gd`, `runtime_probe.gd`, `runtime_identity_context.gd`

Handshake and data messages bind the configured project hash, active run ID, editor debugger-session ID, probe protocol version, supported command/limit set, per-process nonce, and per-request ID. Responses repeat the identities required to reject stale/replayed/replacement-session data.

The gateway supports one active debugger session and validates hello/capabilities before routing. The probe validates every accepted request against its configured identity context. Any field or protocol-version change must be synchronized across gateway, probe, capabilities, Python expectations, and integration tests.

## Type: runtime inspection page

**Sources:** `runtime_tree_service.gd`, `runtime_scene_inspector.gd`

Run-bound specialization of the common inspection page. Results include runtime project/run/session/probe/request identity plus tree/property records, snapshot, offset/continuation data, and bounded metadata.

The editor adapter preflights cursors before sending a request and completes/updates cursor state only after a validated probe response. Runtime object IDs are hashes scoped to the current runtime identity; they are not Godot instance IDs and must not survive a replacement object/run/session.

## Types: gameplay validation requests

**Sources:** `runtime_gameplay_commands.gd`, runtime capture/input/condition services; schemas in `tool_catalog.py`

- Capture: exact `run_id` plus bounded output width/height/pixels; output is a fixed-path staged PNG record.
- Input: exact `run_id`, existing Input Map action, strength, pressed state, and bounded frame or millisecond hold.
- Condition: runtime scope/run ID, bounded timeout, and exactly one fixed type: play state, node existence, node count, or built-in scalar property comparison.

There is no generic expression, script, method, signal wait, regex, nested property traversal, or composable boolean grammar. All requests are identity-bound and bounded independently of the bridge client deadline.

## Type: deferred runtime response

**Sources:** `bridge_server.gd`, `runtime_debugger_gateway.gd`

Private editor-side contract used when a bridge command cannot complete until the debug-run probe responds. The routed handler returns a private deferred marker with a request identity; the bridge retains that authenticated client, while the gateway stores bounded pending identity/deadline/context state.

A probe response releases the client only after all runtime identities and result bounds validate. Timeout, stopped/replaced session, protocol mismatch, or shutdown resolves/cleans the pending record with a stable error. The marker is never part of public MCP or probe results.
