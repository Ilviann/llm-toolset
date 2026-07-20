# Local project and process services

## Purpose

Perform the two operations intentionally kept on the Python host: confined staged asset publication and optional launch of the configured Godot editor.

## Owned source

- `godot_editor_mcp/assets.py` — project/import-root guards, folder creation, bounded streaming copy, and atomic no-replace publication.
- `godot_editor_mcp/launcher.py` — live bridge probe and detached editor startup.

## Dependencies

Both expose narrow interfaces consumed by the tool dispatcher. Asset handling uses the shared domain-error boundary. The launcher uses the bridge to avoid duplicate editor starts.

## Invariants

- All paths remain inside configured roots after traversal and symlink checks.
- Protected `.godot` and `addons` destinations are rejected.
- One file is imported at a time, bounded to 100 MiB and copied in bounded chunks.
- Final publication never overwrites an existing destination on POSIX or Windows, including races.
- `start_editor` accepts no model-provided executable or arguments and uses only an absolute executable from `GODOT_EXECUTABLE`.
- POSIX sessions and Windows process groups remain isolated through explicit platform branches.

## Change and verification guide

Review filesystem security, memory bounds, durability, platform behavior, dispatcher policy, and README setup when changing this component. Run `tests.test_assets`, `tests.test_launcher`, and `tests.test_server`; test every platform branch with mocks and record native validation when available.
