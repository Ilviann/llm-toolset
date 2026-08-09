# Python contracts

Use the index to retrieve only the contract section relevant to the task.

## Python wire contracts

`ProjectLayout` resolves either one `.uproject` descriptor or a folder containing exactly one descriptor. Generated state is always `<Project>/Saved/UnrealMCP/`. Its immutable `ProjectIdentity` contains the descriptor filename stem as `name` and the existing platform-normalized SHA-1 path identity as `project_hash`; it never exposes the absolute descriptor path.

The Python-composed `capabilities` result always includes `project_name`, `project_hash`, `python_version`, `mcp_protocol_version`, `access_mode` (`readonly` or `writable`), `editor_lifecycle`, and `native_capabilities_available`. Access and lifecycle are immutable independent startup dimensions. When the authenticated native call succeeds, the result also contains the native fields, `native_capabilities_available: true`, and Boolean `version_match`. An `editor_unavailable` failure instead returns `bridge_ready: false` and `native_capabilities_available: false`, omitting `version_match` and every native-only field. Authentication, configuration, timeout, cancellation, version, and invalid-response failures are not converted to offline capability responses.

`DiscoveryRecord` has exactly `project_hash` (40 lowercase hex characters), `process_id` (positive integer), `port` (1–65535), `bridge_version` (1–32 characters), `unreal_version` (1–128 characters), and `updated_at_ms` (positive integer). Records older than ten seconds, more than two seconds in the future, or naming a dead process are unavailable.

The token is exactly 64 lowercase hexadecimal characters and is never included in results. `UnrealBridge.call` accepts the twenty-five model-facing commands released through `readonly-mode` plus internal `editor_shutdown`; MCP dispatch first restricts that native allowlist to the configured public catalog. Python schemas additionally enforce exact asset-reference target/cursor shapes, exact stale-safe asset-delete arguments, exact level discovery/current/actor-list/actor/component/cursor shapes, map-qualified Actor GUIDs, bounded exact filters/properties, exact mounted World paths for `level_open`, exact blank/template/configure shapes for `level_manage`, exact bounded `level_actor_edit` discriminators and `level_save` expectations, RPC metadata, typed Actor/component replication forms, exact complete function/macro/custom-event/native-event replacement plans and external links, thirteen exact widget-tree operations, fixed project-hash/current-class framework assignment, exact struct/table game-data operations with four-level recursive row values, and the lifecycle discriminator.

Every mutation uses one caller-generated 32-lowercase-hex `operation_id`. Existing-asset mutations also carry the current 40-lowercase-hex `expected_snapshot`. `UnrealBridge` caches the last reported `bridge_instance_id`; a transport timeout on a mutation is surfaced as `outcome_unknown` with the operation/bridge identity available for reconciliation rather than as permission to retry.

`operation_status` and `operation_cancel` both require exactly one 32-lowercase-hex `operation_id` and one 32-lowercase-hex `bridge_instance_id`. Status lookup is readonly and cannot cancel; cancellation is a separate writable-only MCP tool.

Every model-facing error is `{code,message,details,retryable}`. Codes are defined by `ErrorCode`; messages are limited to 512 characters and details to 16 primitive fields. Stable schema/row codes include `invalid_schema`, `referenced_schema`, `invalid_row`, and `data_limit_exceeded`. Unknown native codes become `internal_error`.

## Platform adapter

`PlatformAdapter` owns the only Python platform branches. Windows path identities use slash normalization and case folding; macOS and Linux preserve case. Process liveness uses `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` on Windows and signal zero on Unix. Detached editor launch uses one fixed executable/project argument pair, no shell, null standard streams, and a project working directory; Windows adds detached/new-process-group flags and macOS starts a new session. Linux rejects launch. Tests inject process and launch probes and exercise all three system names without depending on host process state.

## Editor lifecycle contracts

`editor_lifecycle` accepts exactly `operation_id` (32 lowercase hexadecimal characters) and `operation` (`launch`, `shutdown`, `restart`, or `cancel`). `cancel` addresses the active or retained record with the same ID; it does not identify a process.

Launch uses the startup-configured executable and resolved project descriptor as a fixed two-element argument array with `shell` disabled. A terminal result reports the operation/project/version, state, bounded timestamps, process ID, and old/new bridge-instance IDs. States include `accepted`, `starting`, `shutdown_preflight`, `shutting_down`, `launching`, `ready`, `already_running`, `stopped`, `already_stopped`, `cancelled`, `timed_out`, `rejected`, `failed`, and `outcome_unknown`.

The durable record is `{version:1,records:[...]}` at `Saved/UnrealMCP/lifecycle.json`. It is at most 32 KiB, contains at most 16 exact records, retains them for 24 hours, rejects symbolic-link targets, and uses same-directory atomic replacement. It is separate from the bridge process's mutation ledger.
