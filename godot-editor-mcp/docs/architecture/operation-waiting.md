# Asynchronous operation waiting

## Purpose

Validate concise editor/reload payloads and turn accepted asynchronous operations into bounded scene, import, run, stop, and reload completion waits.

## Owned source

- `godot_editor_mcp/state_payloads.py` — immutable typed views over state/import/operation/run/project/bridge/reload fields.
- `godot_editor_mcp/waiting.py` — deadlines, polling, cancellation, completion predicates, diagnostic settling, and reload reconnect.

## Dependencies

Uses only a bridge-compatible protocol and the shared error model. The dispatcher injects and invokes it. Payload shapes are contract-coupled to editor state trackers, operation/import registries, run identity, and reload persistence.

## Invariants

- Every field used by a predicate is validated before comparison.
- One monotonic deadline covers the complete wait, including reconnect.
- Shutdown cancellation terminates outstanding waits.
- Scene/import/run/stop success is tied to the accepted operation identity, not merely a coincidental state.
- Run waits validate startup survival and allow a bounded diagnostic quiet period.
- Reload reconnect validates project hash, bridge version, and exact operation ID.
- Extra payload fields are tolerated; missing or malformed required fields fail closed.

## Change and verification guide

Whenever editor state or reload wire data changes, update the matching payload reference and both language implementations. Run `tests.test_state_payloads`, `tests.test_waiting`, and relevant server tests. Use the subprocess integration check for reload/reconnect changes.
