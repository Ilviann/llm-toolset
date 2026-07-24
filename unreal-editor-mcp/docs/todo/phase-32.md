# Phase 32 — Retained operations and single-process multiplayer PIE lifecycle

**Outcome:** Agents can start, observe, reconcile, and stop a bounded single-process PIE session, including one listen-server host and one remote client.

### Implementation

- Extend the operation ledger or add a separate retained-operation coordinator for asynchronous editor work. Support bounded `starting`, `running`, `stopping`, terminal, timeout, and safe-cancellation states without blocking the Game thread or one HTTP request until completion.
- Add `play_session_start` and `play_session_stop`. Require caller-generated operation IDs; make identical start and stop replays non-executing and conflicting ID reuse reject.
- Define exact discriminated session topologies for standalone, listen server, dedicated server with owned clients, and client attachment only to a compatible retained session. Do not accept arbitrary remote addresses or command-line arguments.
- Accept an exact mounted map, player/client count, `single_process`, optional compatible GameMode override, and bounded startup/connection timeout. Publish the effective PIE settings rather than silently relying on mutable user preferences.
- Use transient play settings and restore editor settings after the request. Track session ID, bridge instance, PIE instance, world-context identity, process identity, net mode, connection state, local player, controller, and startup diagnostics.
- Use Unreal PIE lifecycle delegates and world contexts to advance state without blocking. Clean every partially created world after failed startup and detect leaked PIE worlds during stop.
- Refuse start while the map is dirty, another PIE/simulation session exists, saving/GC/loading is active, or level mutation conflicts. Keep normal Blueprint and level mutations unavailable while a retained PIE session is active.
- Advertise only `single_process` until Phase 35 completes; reject `multi_process` with a stable capability error.

### Verification

- Test every topology shape, player limits, invalid combinations, map and GameMode validation, transient setting restoration, readiness, connection timeout, partial startup cleanup, replay, conflict, lost response, status polling, cancellation, stop replay, abnormal end, and leaked-world detection.
- Run standalone, two-player listen-server, and dedicated-server-with-clients lifecycle cases natively on macOS and Windows. Verify Linux compatibility code without claiming native coverage.
- Prove a two-player listen server reports two worlds: the listen-server/host world and one remote-client world. Use a dedicated server plus two clients when three separate worlds are required.

### Documentation and completion gate

- Document asynchronous operation states, topology semantics, player/client counts, effective PIE settings, session and instance identities, refusal states, cancellation, cleanup, and the single-process capability boundary.
- Add a two-player listen-server example that starts, polls readiness, identifies both worlds, stops, and verifies the editor returned to ready.
- Complete the phase only when repeated two-player listen-server start/stop is idempotent, bounded, fully observable, and returns the editor to ready with no leaked PIE worlds.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
