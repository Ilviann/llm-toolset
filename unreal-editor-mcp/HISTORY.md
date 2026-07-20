# Unreal Editor MCP history

## 0.1.0 — 2026-07-21

- Added the dependency-free Python stdio MCP server with bounded framing, schema validation, discovery, authenticated HTTP calls, structured errors, timeouts, cancellation, and exact-version handling.
- Added the Unreal 5.8 editor plugin with fail-closed per-project credentials, loopback-only HTTP routing, bounded authenticated commands, Game-thread dispatch, discovery heartbeat, and clean route/state teardown.
- Added the read-only `capabilities` and `editor_state` tools; no mutation command is registered.
- Compiled public API probes for HTTPServer, transactions, Kismet and Blueprint utilities, Subobject Data, K2 schema/actions, compiler diagnostics, Asset Registry, and package saving.
- Added Python unit tests, Unreal Automation Tests, and a cross-process macOS acceptance test.
