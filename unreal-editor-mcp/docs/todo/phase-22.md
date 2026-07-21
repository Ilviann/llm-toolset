# Phase 22 — Optional graceful editor shutdown

**Outcome:** Agents can opt in to gracefully shutting down only the configured authenticated editor instance without forced termination or data loss.

### Implementation

- Extend `editor_lifecycle` with a typed `shutdown` operation while keeping the tool in opt-in large mode.
- Implement bridge-owned graceful shutdown with bounded dirty-package summaries and explicit refusal while unsafe editor work is active.
- Refuse shutdown during active compilation, save, PIE, transaction, or other live state that cannot be proven safe. Do not provide forced process termination.
- Reconcile the accepted shutdown operation across the expected bridge disconnect and return a bounded terminal outcome.

### Verification

- Test clean and dirty packages, active compilation/save/PIE, repeated shutdown, timeout, cancellation before acceptance, disconnect reconciliation, and abnormal termination.
- Run graceful shutdown and recovery natively on macOS and Windows.
- Prove no tool argument can select another process or request forced termination.

### Documentation and completion gate

- Document dirty-content policy, refusal states, disconnect semantics, cancellation, recovery, and default-mode exclusion.
- Complete the phase only when configured graceful shutdown succeeds or refuses safely without arbitrary process control or data loss on native macOS and Windows.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
