---
feature_id: editor-restart
status: completed
depends_on:
  - editor-launch
  - editor-shutdown
released_in: "0.17.0"
---

# `editor-restart` — Optional durable editor restart

**Implementation status:** Completed in 0.17.0 and verified natively on macOS and Windows.

**Outcome:** Agents can opt in to durably restarting the configured project/editor instance and reconcile the full shutdown, disconnect, launch, and readiness sequence.

**Depends on:**

- [`editor-launch`](editor-launch.md)
- [`editor-shutdown`](editor-shutdown.md)

### Implementation

- Extend `editor_lifecycle` with a typed `restart` operation composed from the `editor-shutdown` and `editor-launch` contracts.
- Store exact project identity, Python/plugin version, old and new bridge instances, operation identity, and bounded progress in a durable lifecycle record.
- Reconcile disconnect, rediscovery, reauthentication, exact-version matching, cancellation, timeout, abnormal termination, and final readiness.
- Keep lifecycle operation retention separate from the process-scoped Blueprint mutation ledger and clean stale durable records safely.

### Verification

- Test restart success, dirty or unsafe-state refusal, stale durable records, cancellation at every safe point, timeout, version mismatch, abnormal termination, reconnection, and recovery.
- Run restart, rediscovery, reauthentication, and stale-record recovery natively on macOS and Windows.
- Prove a restart cannot retarget another executable, project, process, port owner, or authenticated bridge.

### Documentation and completion

- Document durable restart states, records, dirty-content interaction, cancellation, recovery, limits, and default-mode exclusion.
- Feature completion requires the implemented, documented, and automated durable-restart contract. Missing applicable native platform verification is tracked separately in the roadmap platform test backlog.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
