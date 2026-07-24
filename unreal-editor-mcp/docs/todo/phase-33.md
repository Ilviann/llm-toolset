# Phase 33 — Per-world runtime actor inspection and attributed diagnostics

**Outcome:** Agents can inspect bounded runtime actor state in one exact retained PIE instance without confusing server and client worlds.

### Implementation

- Add `play_session_inspect` with exact session-summary, instance, actor-list, actor, property, and log-page modes.
- Require both `session_id` and exact instance/world identity for every world-specific query. Reject stale, ended, foreign-bridge, or ambiguous identities.
- Maintain bounded session-scoped actor handles backed by weak object identity and generation. Return class, name, transform, owner, local role, remote role where meaningful, authority, dormancy, relevancy settings, replication state, and destruction state.
- Support exact filters for runtime actor identity, class, tag, name, owner, authority, local role, and network role. Bound actor scans, retained handles, requested properties, nested values, pages, cursors, log events, and response bytes.
- Reuse the safe reflected-value codec for selected read-only runtime properties. Reject arbitrary traversal, transient internals, object graphs, delegates, unsafe references, and unrestricted replication internals.
- Correlate replicated counterparts only when Unreal exposes a reliable session-scoped network identity. Otherwise return per-world identities and explicit `correlation_unavailable` rather than guessing from object names.
- Capture bridge-generated lifecycle, connection, command, test, warning, and error events in bounded per-instance buffers. Include raw engine logs only when their originating process/PIE instance can be proven; exclude unattributable entries.

### Verification

- Test session and instance identity requirements, server/client separation, actor lifecycle, handle staleness, every filter, authority and roles, ownership, dormancy, relevancy, properties, destruction, cursor expiry, log bounds, and unattributed-log exclusion.
- Inspect one replicated actor in the listen-server/host and remote-client worlds, and in dedicated-server plus two-client topology for the three-world case.
- Prove identical names in different worlds cannot be confused and every returned diagnostic has a verified originating instance.

### Documentation and completion gate

- Document runtime identities, world requirements, actor handle lifetime, replication fields, correlation limits, property policy, filters, log attribution, pagination, and limits.
- Add an example that inspects the same replicated gameplay class independently in the listen-server/host and remote-client worlds.
- Complete the phase only when authoritative and client copies can be inspected independently with no implicit world selection or unbounded runtime traversal.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
