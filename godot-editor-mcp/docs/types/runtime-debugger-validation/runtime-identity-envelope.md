# Type: runtime identity envelope

**Sources:** `runtime_debugger_gateway.gd`, `runtime_probe.gd`, `runtime_identity_context.gd`

Handshake and data messages bind the configured project hash, active run ID, editor debugger-session ID, probe protocol version, supported command/limit set, per-process nonce, and per-request ID. Responses repeat the identities required to reject stale/replayed/replacement-session data.

The gateway supports one active debugger session and validates hello/capabilities before routing. The probe validates every accepted request against its configured identity context. Any field or protocol-version change must be synchronized across gateway, probe, capabilities, Python expectations, and integration tests.
