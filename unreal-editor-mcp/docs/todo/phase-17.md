# Phase 17 — User-defined structs and Data Tables

**Outcome:** Agents can define bounded game-design row schemas and create, inspect, and update typed Data Tables for values such as weapon damage, ammunition, progression, and leveling curves.

**Status:** Completed in 0.16.0.

### Implementation

- Add `game_data_inspect` and `game_data_edit` with exact `user_defined_struct` and `data_table` target discriminators instead of one tool per row or field operation.
- Create, inspect, compile, save, and safely mutate user-defined struct assets whose members use a bounded extension of the canonical K2 type/default codec. Support identity-based member add, rename, type/default update, reorder where Unreal preserves it safely, and reject-only removal/type-change policies when dependent assets would be invalidated.
- Create Data Tables from one exact live native or user-defined `FTableRowBase` descendant. Inspect the row-struct identity, schema, and bounded pages of named rows without emitting unrestricted Unreal serialization.
- Add transactional bounded row operations for add, replace, rename, remove, and batch upsert/remove. Validate every field against the live row property, preserve unspecified fields when requested explicitly, reject duplicate/case-conflicting row names, and return concise changed-row read-back.
- Extend the reflected value codec only as needed for bounded nested row data: scalars, enums, common and user-defined structs, arrays, sets, maps, and compatible asset/class references. Publish depth, collection, field, row, scan, request, and response limits; reject unsupported instanced object graphs and arbitrary serialized text.
- Scan and report bounded Blueprint/Data Table dependencies before destructive schema edits. Use operation reconciliation, exact asset/schema snapshots, editor transactions where supported, package saving, and explicit restoration for unexpected failures.
- Keep CSV/JSON filesystem import/export, Curve Tables, Data Assets, arbitrary UObject assets, and supplied struct code outside this phase.

### Verification

- Build representative weapon-stat, damage, ammunition, and level-progression schemas and tables; inspect, batch-edit, compile/save, restart, and read back exact typed values and references.
- Test native and user-defined row structs, schema evolution, dependent tables/Blueprint references, duplicate names, unsupported types, missing fields, nested-depth and collection bounds, stale snapshots, partial-page cursors, and response truncation.
- Test atomic batch failure, rollback/restoration, undo/redo where supported, save failure, timeout, replay, lost response, reload, and deletion/type-change rejection with unchanged dependent content.
- Run the complete game-data suite through Python contract tests, native Automation, and the production bridge headless workflow.

### Documentation and completion gate

- Document schema design, supported values, row operations, batching, dependency policy, limits, save/recovery behavior, and focused shooter-balance examples.
- Complete the phase only when agents can create and maintain representative typed balance tables without filesystem import, unsafe serialization, or partial batch results.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
