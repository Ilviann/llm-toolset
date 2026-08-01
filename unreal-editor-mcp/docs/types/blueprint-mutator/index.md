# Blueprint mutator contracts

- [Creation, compile, and save contracts](contracts.md#creation-compile-and-save-contracts) — creation, compile, save, result, diagnostic, scope, and failure-cleanup contracts.
- [Component and Blueprint-default mutation contracts](contracts.md#component-and-blueprint-default-mutation-contracts) — component ownership, hierarchy operations, snapshot preconditions, transactions, and class defaults.
- [Reflected default-property codec](contracts.md#reflected-default-property-codec) — safe editable property policy, JSON forms, references, and round trips.
- [Canonical K2 member, parameter, and local type/default codec](contracts.md#canonical-k2-member-parameter-and-local-typedefault-codec) — canonical Blueprint member types, tagged defaults, containers, and references.
- [Blueprint member-variable contracts](contracts.md#blueprint-member-variable-contracts) — stable member identities, operations, metadata, replication, references, and reject-only safety.
- [Function-signature and local-variable contracts](contracts.md#function-signature-and-local-variable-contracts) — user-function ownership, complete signatures, required nodes, locals, RepNotify coupling, and reject-only reference safety.
- [Macro and custom-event contracts](contracts.md#macro-and-custom-event-contracts) — macro tunnels/signatures and event-graph-bound custom-event shells, identities, metadata, ownership, and reject-only reference safety.
- [Blueprint reference scanner](contracts.md#blueprint-reference-scanner) — typed bounded variable/function/local/macro/custom-event reference scans shared by inspection and mutation.
