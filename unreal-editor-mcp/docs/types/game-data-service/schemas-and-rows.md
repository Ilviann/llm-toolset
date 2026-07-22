# User-defined struct and Data Table contracts

`game_data_inspect` accepts one exact initial target or a cursor continuation. A user-defined struct result pages identity-bearing member records. A Data Table result includes the exact row-struct path and kind, its bounded reflected schema, and a page of rows sorted by name. Optional `row_names` selects at most 64 exact rows without changing the asset-wide `snapshot_id`. Continuations use only `cursor` and optional `page_size`.

`game_data_edit` always carries a caller-generated operation ID. Creation uses a long package name; existing assets use an object or package path plus the latest 40-hex snapshot.

User-defined struct operations are:

- `create` with one-to-64 complete member declarations;
- `add_member` with one declaration;
- `rename_member` by stable member ID;
- `update_member` for exactly `type` plus `reject_if_referenced`, or `default`;
- `reorder_member` above or below another stable member ID; and
- `remove_member` with `reject_if_referenced`.

Member declarations use the shared canonical K2 type and tagged default forms. Reference/const member types, nested containers, unsupported K2 categories, duplicate/case-conflicting friendly names, missing identities, recursive invalid structures, and an empty final structure reject. Type change and removal scan at most 256 package referencers and reject on any dependency or truncation. Member add, rename, default update, and safe reorder rely on Unreal's public structure compiler to preserve dependent content.

Data Table operations are `create`, `add_row`, `replace_row`, `rename_row`, `remove_row`, and `batch`. `batch` carries at most 64 combined upserts/removals. Every write is staged against the exact live schema before the transaction begins. Duplicate names, case conflicts, missing/extra fields, incompatible values, overlapping remove/upsert names, and invalid preserve requests reject the whole request without partial rows.

Every successful edit compiles when applicable, saves non-interactively, re-inspects, and returns the new snapshot. Filesystem CSV/JSON import/export, Curve Tables, Data Assets, arbitrary UObject assets, and supplied struct code remain unavailable.
