# Asset, inspection, and scene services

## Purpose

Expose bounded asset discovery/creation, explicit edited/runtime inspection, initial scene construction, focused edits, and atomic multi-operation scene transactions.

## Owned source

- `asset_commands.gd` — asset pages/info/scans, resource creation, and scene opening.
- `edited_scene_inspector.gd` — edited reads and explicit scope routing.
- `scene_commands.gd` — initial scene construction, focused transaction adapters, and selection.
- `scene_transaction.gd` — shadow-prevalidated atomic scene mutations and UndoRedo commit/rollback.

## Dependencies

Uses shared path/node/value/cursor/limit/error/operation infrastructure. Asset work receives import/filesystem callbacks. Scene work receives scene-state and UndoRedo collaborators. Runtime inspection is injected through the runtime component. Python tool policy, local imports, and waits mirror command/result shapes.

## Invariants

- Asset/tree/property reads are bounded, targeted, paginated, and tied to query/snapshot cursors.
- Inspection is read-only; mutation logic lives only in scene services.
- `create_scene` validates the complete bounded in-memory tree before writing.
- Focused tiny-mode edits translate into the same transaction representation as batch edits.
- Transactions preflight against an isolated PackedScene shadow and do not open an UndoRedo action until validation succeeds.
- A successful transaction is one scene-associated undo step; failed validation leaves scene and history unchanged.
- Ownership, inherited/instanced child, root, signal/method, class, resource, reference, size, and retained-undo rules fail closed.
- Unexpected postconditions trigger immediate rollback.

## Change and verification guide

Update tool schemas, value/transaction type references, command limits, capabilities, and ownership tests together. Run schema/contract/server tests plus Phase 8 service boundary and Phase 11 transaction tests. Use the integration test for persistence, runtime-scope, cursor, or complete transaction changes.
