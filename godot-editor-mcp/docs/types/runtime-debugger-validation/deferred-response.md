# Type: deferred runtime response

**Sources:** `bridge_server.gd`, `runtime_debugger_gateway.gd`

Private editor-side contract used when a bridge command cannot complete until the debug-run probe responds. The routed handler returns a private deferred marker with a request identity; the bridge retains that authenticated client, while the gateway stores bounded pending identity/deadline/context state.

A probe response releases the client only after all runtime identities and result bounds validate. Timeout, stopped/replaced session, protocol mismatch, or shutdown resolves/cleans the pending record with a stable error. The marker is never part of public MCP or probe results.
