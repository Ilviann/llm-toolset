# Types: domain error hierarchy

**Source:** `godot_editor_mcp/errors.py`

`DomainError` is the expected-failure boundary. It carries a stable `code`, public `message`, bounded JSON-safe `details`, and `retryable` flag. `AssetError` and `LauncherError` represent local services; `BridgeError` and its typed subclasses represent mirrored editor failures such as invalid arguments, protected/not-found resources, busy/import/run states, stale cursor/runtime/scene/operation identities, timeouts/cancellation, save/transaction failures, version/project mismatch, unsupported capabilities, unavailable/ambiguous runtime probes, and invalid responses.

`ErrorCode` centralizes strings mirrored by `error_envelope.gd`. Unknown editor codes remain generic `BridgeError`. Adding a code requires both languages, capabilities/contract expectations where applicable, bounded detail policy, and tests.
