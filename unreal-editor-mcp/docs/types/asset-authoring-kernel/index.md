# Asset-authoring kernel contracts

## Creation records

- `FUnrealMCPAssetCreationRequest` carries one optional already-ledger-admitted operation identity plus the exact canonical package and object paths.
- `FUnrealMCPAssetCreationHooks` supplies typed semantic creation, optional finalization, mandatory persistence, and authoritative snapshot read-back callbacks.
- `FUnrealMCPAssetCreationResult` returns only the exact created asset, canonical object path, and verified snapshot for domain-specific result encoding.

## Editing records

- `FUnrealMCPAssetEditRequest` carries one optional already-ledger-admitted operation identity, exact loaded asset/path, required expected snapshot, bounded transaction label, persistence choice, and changed-snapshot policy.
- `FUnrealMCPAssetEditHooks` supplies optional family-specific unsafe-state validation plus authoritative snapshot, typed semantic mutation, and persistence callbacks.
- `FUnrealMCPAssetEditResult` returns the admitted and verified postcondition snapshots. Domain services retain stable subobject identities and concise changed-value records.

## Lifecycle errors

Shared admission retains stable `invalid_argument`, `mutation_scope_denied`, `write_conflict`, `already_exists`, `not_found`, `busy`, `stale_precondition`, `no_change`, `compile_failed`, `save_failed`, `internal_error`, and `rollback_failed` categories. Semantic adapters may return their narrower existing family errors. A failed mutation preserves its original error only when exact rollback succeeds; failed restoration becomes `rollback_failed`.
