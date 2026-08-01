# Project-workflow and reload contracts

Use the index to retrieve only the contract section relevant to the task.

## Type: project settings patch

**Source:** `project_settings_commands.gd`; schema in `tool_catalog.py`

Request contains up to the published limit of changes, plus `save` and `dry_run`. Each change names one permitted key, provides a bounded property value, and may include `expected` for compare-and-swap protection.

The full batch is normalized and validated before mutation. Results report the normalized diff and required refresh/reload level. A stale expected value rejects everything; failed save restores all original values. Secret/internal keys and general Input Map keys are excluded from this type.

## Type: Input Map patch

**Source:** `input_map_commands.gd`; schema in `tool_catalog.py`

Request identifies one action, optional deadzone, normalized events to add/remove, and save behavior. Event values use the shared input-event contract. The operation preserves unrelated events, treats normalized exact duplicates deterministically, updates live `InputMap`, and saves through `ProjectSettings` transactionally.

The result reports normalized action state/diff and refresh/reload requirements. Validation or save failure leaves the original action configuration in place.

## Types: autoload record and patch

**Source:** `project_workflow_commands.gd`; schema in `tool_catalog.py`

Public records contain name, normalized `res://` path, singleton flag, compare-and-swap `value`, and protected status. A patch contains bounded `add`, `update`, or `remove` changes; add/update require an existing Node script or PackedScene path, and every change may state the expected prior value.

The complete virtual result is validated before guarded `EditorPlugin` APIs apply it. Invalid/conflicting/protected names or paths, stale expectations, application mismatch, or save failure reject/roll back the entire batch. The reserved runtime probe remains immutable through this type.

## Types: reload record and status

**Sources:** `reload_commands.gd`, `state_payloads.py`, `waiting.py`

Before restart, the plugin atomically persists a bounded pending record containing record version, normalized project identity, exact bridge version, operation ID, process/time freshness data, and pending state. It never persists credentials.

Startup accepts only a fresh matching record, restores completion into the new process's operation registry, and exposes a status carrying the same project/version/operation identities. Python reconnect succeeds only when all identities match the accepted request. Malformed, stale, cross-project, cross-version, or inconsistent states have distinct errors and cannot become ambiguous success.
