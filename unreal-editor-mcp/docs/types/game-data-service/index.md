# Game-data service contracts

`UnrealMCPGameDataRequestValidation` owns exact request shapes and normalized primitives. `UnrealMCPGameDataInspectionBuilder` owns schema/record encodings, dependency metadata, snapshots, and saved-edit result envelopes; operation handlers consume those builders for precondition and postcondition read-back.

- [User-defined struct and Data Table contracts](contracts.md#user-defined-struct-and-data-table-contracts) — targets, identities, snapshots, operations, dependency policy, batching, and paging.
- [Bounded reflected row-value codec](contracts.md#bounded-reflected-row-value-codec) — bounded recursive reflected row-value forms, references, and limits.
