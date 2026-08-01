# Blueprint graph-editor contracts

Exact request normalization is owned by `UnrealMCPBlueprintGraphRequestValidation`; node and pin handlers consume only its typed request. `UnrealMCPBlueprintGraphResultBuilder` is the sole owner of changed-node records, sorted created/reconstructed identities, and the common graph-edit result envelope.

- [Graph-node lifecycle contracts](contracts.md#graph-node-lifecycle-contracts) — exact add/move/remove shapes, action re-resolution, mutable targets, identity completion, bounds, transactions, change records, and re-inspection.
- [Pin-default and direct-connection contracts](contracts.md#pin-default-and-direct-connection-contracts) — exact pin-default/connect/disconnect shapes, live wildcard/reconstruction behavior, opt-in bounded conversion, cycle policy, bounds, and results.
