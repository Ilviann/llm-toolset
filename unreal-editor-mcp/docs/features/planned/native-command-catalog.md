---
feature_id: native-command-catalog
status: planned
depends_on:
  - native-wire-contracts
released_in: null
---

# `native-command-catalog` — Fixed native command routing and capabilities

**Outcome:** The bridge dispatches through fixed typed command descriptors instead of directly composing every domain service, command branch, and capability field.

**Depends on:**

- [`native-wire-contracts`](native-wire-contracts.md)

### Implementation

- Define fixed descriptors for command identity, access, retained-operation policy, handler, limits, and native capabilities.
- Keep authentication, body bounds, queueing, Game-thread dispatch, the operation ledger, response bounds, and final capability authority in the host bridge.
- Replace the bridge's domain includes, dispatch switch, mutation-command classification, and hand-built feature list with deterministic catalog composition.
- Reject duplicate commands, conflicting capabilities, late registration, and runtime-provided commands or schemas. Python remains authoritative for the shipped model-facing schemas.

### Verification and completion gate

- Prove exact command, readonly/writable, ledger, capability, and error parity through native and Python contract tests.
- Run all bridge, companion, lifecycle, native Automation, headless, build, and packaging gates.
- Complete only when adding a fixed domain handler no longer requires editing `UnrealMCPBridge` and no dynamic tool surface exists.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
