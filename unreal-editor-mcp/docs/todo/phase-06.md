# Phase 6 — Function signatures and local variables

**Outcome:** Agents can inspect and define user-owned function shells, complete signatures, and typed local variables.

### Implementation

- Extend `blueprint_inspect` with bounded function-signature, parameter, local-variable, metadata, ownership, required-node, and reference-summary records.
- Add `blueprint_member_edit` operations for one add, rename, supported update, or safe removal of a user-owned function or one local variable.
- Create function graphs through public Blueprint utilities, preserve required entry and result nodes, and validate the complete signature before mutation.
- Extend the canonical K2 vocabulary with parameter direction, const/reference, and local-scope forms. Validate access, pure/const flags, inputs, outputs, defaults, and local scope through live Blueprint capabilities.
- Complete RepNotify mutation support by validating the notification-function signature and its relationship to the owning member variable.
- Keep override events, interface functions, inherited functions, and user-owned functions distinct. Use `reject_if_referenced` for signature changes and removal.
- Return concise function, graph, parameter, local-variable, reference, reconstruction, and snapshot records.

### Verification

- Test every supported parameter direction, function flag, signature form, local-variable type and scope, invalid collision, required graph node, and unsupported capability.
- Test local-variable references, RepNotify coupling, used functions, safe removal, rejected signature changes or removal, undo/redo, compilation, saving, restart, and read-back.
- Prove that rejected operations preserve structure, dirty state, compile state, and transaction history.

### Documentation and completion gate

- Document function ownership, signatures, locals, RepNotify coupling, reference policy, and inspect-before-edit examples.
- Complete the phase only when function signatures and local variables survive compile, save, restart, and exact bounded inspection.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
