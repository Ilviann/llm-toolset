# Type: error envelope

**Source:** `plugin/addons/godot_mcp/error_envelope.gd`

Editor handlers return a common dictionary boundary. Success contains `ok: true` and a result. Failure contains `ok: false` plus stable `code`, public `message`, bounded JSON-safe `details`, and `retryable`.

Codes mirror Python `ErrorCode`; changing them requires contract tests and typed Python decoding updates. Construction and compatibility helpers are documented separately in [`error-envelope-library.md`](error-envelope-library.md).
