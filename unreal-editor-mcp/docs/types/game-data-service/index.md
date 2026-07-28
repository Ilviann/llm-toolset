# Game-data service contracts

`UnrealMCPGameDataRequestValidation` owns exact request shapes and normalized primitives. `UnrealMCPGameDataInspectionBuilder` owns schema/record encodings, dependency metadata, snapshots, and saved-edit result envelopes; operation handlers consume those builders for precondition and postcondition read-back.

- [`schemas-and-rows.md`](schemas-and-rows.md) — targets, identities, snapshots, operations, dependency policy, batching, and paging.
- [`value-codec.md`](value-codec.md) — bounded recursive reflected row-value forms, references, and limits.
