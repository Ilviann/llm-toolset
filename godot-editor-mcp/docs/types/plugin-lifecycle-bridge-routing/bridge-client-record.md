# Type: bridge client record

**Source:** `plugin/addons/godot_mcp/bridge_server.gd`

Internal dictionary retained for each accepted TCP peer. It contains the peer, receive buffer, acceptance/deadline time, and state needed to parse exactly one authenticated newline-delimited request. A validated runtime request may additionally remain associated with a deferred request identity until the debugger gateway resolves it.

The collection is capped at 16. Incomplete/unauthenticated requests expire after two seconds, excess clients are rejected, buffers are bounded, and shutdown disconnects every record. Fields are private implementation data and must not cross the bridge.
