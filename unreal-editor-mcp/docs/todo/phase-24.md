# Phase 24 — Optional durable editor restart

**Outcome:** Agents can opt in to durably restarting the configured project/editor instance and reconcile the full shutdown, disconnect, launch, and readiness sequence.

### Implementation

- Extend `editor_lifecycle` with a typed `restart` operation composed from the Phase 23 shutdown and Phase 22 launch contracts.
- Store exact project identity, Python/plugin version, old and new bridge instances, operation identity, and bounded progress in a durable lifecycle record.
- Reconcile disconnect, rediscovery, reauthentication, exact-version matching, cancellation, timeout, abnormal termination, and final readiness.
- Keep lifecycle operation retention separate from the process-scoped Blueprint mutation ledger and clean stale durable records safely.

### Verification

- Test restart success, dirty or unsafe-state refusal, stale durable records, cancellation at every safe point, timeout, version mismatch, abnormal termination, reconnection, and recovery.
- Run restart, rediscovery, reauthentication, and stale-record recovery natively on macOS and Windows.
- Prove a restart cannot retarget another executable, project, process, port owner, or authenticated bridge.

### Documentation and completion gate

- Document durable restart states, records, dirty-content interaction, cancellation, recovery, limits, and default-mode exclusion.
- Complete the phase only when restart reaches the exact configured project bridge without arbitrary process execution or data loss on native macOS and Windows.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
