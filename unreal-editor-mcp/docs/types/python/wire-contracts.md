# Python wire contracts

`ProjectLayout` resolves either one `.uproject` descriptor or a folder containing exactly one descriptor. Generated state is always `<Project>/Saved/UnrealMCP/`.

`DiscoveryRecord` has exactly `project_hash` (40 lowercase hex characters), `process_id` (positive integer), `port` (1–65535), `bridge_version` (1–32 characters), `unreal_version` (1–128 characters), and `updated_at_ms` (positive integer). Records older than ten seconds, more than two seconds in the future, or naming a dead process are unavailable.

The token is exactly 64 lowercase hexadecimal characters and is never included in results. `UnrealBridge.call` accepts the fifteen Phase 17 commands. Python schemas additionally enforce exact RPC metadata, typed Actor/component replication forms, fixed project-hash/current-class framework assignment, and exact struct/table game-data operations with four-level recursive row values.

Every mutation uses one caller-generated 32-lowercase-hex `operation_id`. Existing-asset mutations also carry the current 40-lowercase-hex `expected_snapshot`. `UnrealBridge` caches the last reported `bridge_instance_id`; a transport timeout on a mutation is surfaced as `outcome_unknown` with the operation/bridge identity available for reconciliation rather than as permission to retry.

Every model-facing error is `{code,message,details,retryable}`. Codes are defined by `ErrorCode`; messages are limited to 512 characters and details to 16 primitive fields. Stable schema/row codes include `invalid_schema`, `referenced_schema`, `invalid_row`, and `data_limit_exceeded`. Unknown native codes become `internal_error`.
