# Bridge, discovery, and domain errors

## Purpose

Locate the live project-specific editor bridge, authenticate and exchange bounded localhost messages, and normalize expected failures behind stable typed errors.

## Owned source

- `godot_editor_mcp/bridge.py` — token reading, port selection, bounded socket exchange, and response decoding.
- `godot_editor_mcp/discovery.py` — project identity, heartbeat validation, and port selection.
- `godot_editor_mcp/errors.py` — stable codes, bounded details, typed domain and bridge errors.

## Dependencies

This is a low-level Python component. Higher layers depend on it; it does not depend on MCP presentation or dispatch. It is wire-coupled to editor bridge transport, error envelopes, command limits, discovery records, reload records, and exact versions.

## Invariants

- Connections target localhost only and authenticate with the per-project token.
- Request/response sizes and socket deadlines are bounded.
- Discovery accepts only fresh records matching the normalized project hash; malformed or stale matching records fall back safely, while another-project records are rejected.
- Known editor error codes retain code, message, bounded details, and retryability through MCP presentation.
- Unknown codes degrade to generic bridge errors without trusting unbounded payloads.

## Change and verification guide

Treat transport fields, discovery records, error strings, and identity normalization as mirrored cross-language contracts. Update their type references and Godot implementations together. Run `tests.test_bridge`, `tests.test_discovery`, `tests.test_contracts`, and the reload integration test when persistent identities or reconnect behavior change.
