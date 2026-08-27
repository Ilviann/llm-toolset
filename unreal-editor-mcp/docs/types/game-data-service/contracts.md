# Game-data service contracts

Use the index to retrieve only the contract section relevant to the task.

## User-defined struct, Data Table, and Data Asset contracts

`game_data_inspect` accepts one exact initial target or a cursor continuation. A user-defined struct result pages identity-bearing member records. A Data Table result includes the exact row-struct path and kind, its bounded reflected schema, and a page of rows sorted by name. Optional `row_names` selects at most 64 exact rows without changing the asset-wide `snapshot_id`. A `data_asset` target accepts one exact `UDataAsset` or `UPrimaryDataAsset` and optional one-to-64 exact `property_names`; it returns bounded `property` records plus `class_path`, `primary_data_asset`, `property_count`, and `unsupported_property_count` metadata. Continuations use only `cursor` and optional `page_size`.

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

Every successful edit compiles when applicable, saves non-interactively, re-inspects, and returns the new snapshot. Filesystem CSV/JSON import/export, Curve Tables, Data Asset mutation, generic arbitrary UObject inspection/mutation, and supplied struct code remain unavailable.

## Bounded reflected row-value codec

Top-level row `values` and Data Asset properties use the same bounded reflected inspection codec. Supported values are:

- Boolean, finite numeric, name/string/`FText`, exact enum-name scalars, `FGameplayTag`, and 32-lowercase-hex `FGuid` strings;
- `FGameplayAttribute` as `{kind:"gameplay_attribute",resolved,compatible,name,property_path,owner_path}`, including when used as a container key;
- `FGameplayTagContainer` as a sorted array of explicit tag strings;
- compatible hard/soft object or class references as `{kind:"reference",path:"/…"}`, with an empty path for null;
- arrays as JSON arrays;
- sets as `{kind:"set",items:[…]}`;
- maps as `{kind:"map",entries:[{key:…,value:…}]}`; and
- common, native, or user-defined structs as `{kind:"struct",fields:{…}}`.

Containers hold at most 64 items. Nested values have a maximum depth of four, each nested struct has at most 64 fields, one operation touches at most 64 rows, and inspection refuses tables above the 2,048-row scan ceiling. Numeric writes must be finite, integral for integer properties, within the reflected property's range, and exactly representable in JSON's safe integer range.

Every field is resolved against the live `FProperty`. References must resolve to a compatible visible packageable object/class and must not be transient or editor-only. For inspection, an unsupported property becomes `{kind:"unsupported",type:"…",reason:"…"}` inside a Data Table row, or a `supported:false` Data Asset property record with `error` and `message`; supported siblings still return. Structural limits and invalid requests still reject the whole call. For mutation, instanced references, delegates, interfaces, arbitrary UObject graphs, raw import text, and properties outside the writable codec reject explicitly. `preserve_unspecified: true` begins from the existing row; otherwise staging begins from the row struct's live defaults.
