# Python wire contracts

`ProjectLayout` resolves either one `.uproject` descriptor or a folder containing exactly one descriptor. Generated state is always `<Project>/Saved/UnrealMCP/`.

`DiscoveryRecord` has exactly `project_hash` (40 lowercase hex characters), `process_id` (positive integer), `port` (1–65535), `bridge_version` (1–32 characters), `unreal_version` (1–128 characters), and `updated_at_ms` (positive integer). Records older than ten seconds, more than two seconds in the future, or naming a dead process are unavailable.

The token is exactly 64 lowercase hexadecimal characters and is never included in results. `UnrealBridge.call` accepts only `capabilities`, `editor_state`, or `blueprint_inspect`, sends `{command,arguments}` to `/unreal-mcp/v1/command`, and requires `{ok:true,result}` or `{ok:false,error}`. The Python tool schema enforces the three exact inspection argument families documented under `types/blueprint-inspector/`.

Every model-facing error is `{code,message,details,retryable}`. Codes are defined by `ErrorCode`; messages are limited to 512 characters and details to 16 primitive fields. Unknown native codes become `internal_error`.
