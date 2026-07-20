# Types: state payload views

**Source:** `godot_editor_mcp/state_payloads.py`

- `ImportPayload` validates project-relative path, operation identity, status, and failure fields for one import record.
- `EditorStatePayload` exposes validated scene, filesystem/import, operation, run, diagnostic, project, bridge, and version fields used by wait predicates.
- `ReloadStatusPayload` validates reload state plus project hash, bridge version, operation ID, completion/recovery fields, and cross-field consistency.

These immutable views validate fields lazily when accessed and tolerate unrelated extra fields for forward-compatible observation. A predicate must access every field on which success depends; missing, wrong-type, or inconsistent values become `InvalidResponseError`.
