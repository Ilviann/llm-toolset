# Bridge, discovery, and error contracts

Use the index to retrieve only the contract section relevant to the task.

## Type: `GodotBridge`

**Source:** `godot_editor_mcp/bridge.py`

Concrete `BridgeClient` for one configured project. It reads the project token, selects an explicit or discovered localhost port, emits one bounded newline-delimited JSON request, receives one bounded response, and decodes editor failures.

The normal socket deadline is fixed and short; only the bounded runtime-condition path may extend it within its published maximum. The client validates configuration and response shape, never follows arbitrary hosts, and rereads credentials during reload reconnect through waiter-created calls.

## Type: `DiscoveryRecord`

**Sources:** `godot_editor_mcp/discovery.py`, mirrored by `plugin/addons/godot_mcp/discovery_record.gd`

Immutable validated view of `.godot/godot_mcp_bridge.json`.

The record identifies its schema/bridge version, normalized project hash, editor process, localhost port, and heartbeat time. It never contains the auth token or absolute project path. Reads bound file size and field types/ranges. Freshness and process/project ownership determine whether the discovered port can be used or a record may be removed.

Any field/version change is a cross-language persistent-schema change and requires bridge discovery tests plus live reconnect verification.

## Types: domain error hierarchy

**Source:** `godot_editor_mcp/errors.py`

`DomainError` is the expected-failure boundary. It carries a stable `code`, public `message`, bounded JSON-safe `details`, and `retryable` flag. `AssetError` and `LauncherError` represent local services; `BridgeError` and its typed subclasses represent mirrored editor failures such as invalid arguments, protected/not-found resources, busy/import/run states, stale cursor/runtime/scene/operation identities, timeouts/cancellation, save/transaction failures, version/project mismatch, unsupported capabilities, unavailable/ambiguous runtime probes, and invalid responses.

`ErrorCode` centralizes strings mirrored by `error_envelope.gd`. Unknown editor codes remain generic `BridgeError`. Adding a code requires both languages, capabilities/contract expectations where applicable, bounded detail policy, and tests.

## Library: project identity and discovery selection

**Source:** `godot_editor_mcp/discovery.py`

- `normalized_project_path(project)` produces the canonical path representation with explicit POSIX/Windows behavior.
- `project_path_hash(project)` produces the SHA-256 identity mirrored by Godot.
- `read_discovery_record(project)` parses and validates the bounded heartbeat.
- `discovered_port(project, fallback)` selects a fresh matching record, safely falls back for absent/stale/malformed matching state, and rejects a record for another project.

Normalization is a compatibility contract for discovery and reload; test both platform branches regardless of the host platform.
