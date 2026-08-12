---
feature_id: native-command-catalog
status: completed
depends_on:
  - native-wire-contracts
released_in: "0.38.0"
---

# `native-command-catalog` — Fixed native command routing and capabilities

**Outcome:** The bridge dispatches through a frozen typed command catalog instead of directly composing domain services, command branches, retained-operation classification, and native feature/limit fields.

**Implementation status:** Completed in 0.38.0. Windows passed the Python suite, adaptive/forced-unity/non-unity UE 5.8 builds, full native Automation, full headless lifecycle integration, and isolated Win64 base-plugin packaging. macOS remains preferred follow-up work and Linux is out of scope.

**Depends on:**

- [`native-wire-contracts`](native-wire-contracts.md)

### Implementation

- `FUnrealMCPCommandDescriptor` fixes command identity, access, dispatch, retained-operation policy, handler, extension-request eligibility, native features, and native limits.
- `FUnrealMCPCommandCatalog` lazily owns domain services. `UnrealMCPBridge` retains authentication, body/response bounds, queueing, Game-thread dispatch, operation admission and retention, listener lifecycle, and final capability authority.
- Deterministic descriptor composition supplies command routing, `capabilities.commands`, native features, native limits, and Blueprint-family composition. Adding a fixed base-domain handler no longer edits the bridge.
- Registration freezes before listener startup and rejects duplicate commands, conflicting capabilities or limits, late registration, runtime commands, and native model schemas. Python remains authoritative for shipped model-facing schemas and access filtering.

### Verification and completion gate

- Native catalog Automation covers identity order, access, retained-operation policy, handler composition, features, limits, duplicate/conflict rejection, freeze, and runtime command/schema rejection.
- Python contracts prove exact native/Python command and access parity, ledger classification, request-thread dispatch, capability/limit ownership, error contracts, and absence of domain includes or routing branches in `UnrealMCPBridge`.
- Windows passed all bridge, companion, lifecycle, native Automation, headless, adaptive/forced-unity/non-unity build, and base packaging gates.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
