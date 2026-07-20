# Project workflows and reload

## Purpose

Provide bounded transactional changes to project settings, Input Map, and autoloads; read editor-plugin metadata; and safely persist/recover a project reload across editor restart.

## Owned source

- `project_settings_commands.gd` — bounded reads and compare-and-swap setting patches.
- `input_map_commands.gd` — normalized Input Map mutation.
- `project_workflow_commands.gd` — autoload mutation and read-only plugin metadata.
- `reload_commands.gd` — reload safeguards, persistent operation record, restart, recovery, and status.

## Dependencies

Uses shared value/input codecs, path guards, identities, records, limits, errors, and operation IDs. It receives a project-file-write callback and guarded `EditorPlugin` autoload APIs. Python schemas and wait/reconnect logic mirror these contracts.

## Invariants

- Complete batches are prevalidated before mutation and stale expected values reject the whole batch.
- Failed application or save restores every affected in-memory setting/autoload.
- Secret/internal settings and general Input Map keys are not exposed through generic settings tools.
- Autoload paths must resolve to a Node script or PackedScene; names must not conflict with protected/runtime/built-in identities.
- Editor plugins are metadata-only; arbitrary install/enable/disable is not exposed.
- Reload never discards unsaved scenes and requires explicit authorization to stop a run or save scenes.
- Pending reload records are bounded, atomic, project/version/operation-specific, fresh, and recovered exactly once.

## Change and verification guide

Update schemas, capabilities, reload payload references, README user workflow, and cross-language version contracts. Run server/schema/wait/contract tests, Phase 3 reload-record and Phase 12 project-workflow tests, and the subprocess reload integration check.
