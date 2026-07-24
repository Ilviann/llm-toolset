# Phase 35 — Multi-process PIE companion and cross-process observation

**Outcome:** Agents can use the Phase 32–34 session contracts with bounded multi-process PIE server and client instances through an authenticated local runtime companion.

### Implementation

- Add a minimal exact-version runtime companion module loaded only into editor-owned multi-process PIE children. Keep it free of model-facing listeners, cloud services, accounts, telemetry, runtime downloads, and general remote control.
- Have each owned child connect outward to the editor bridge through bounded localhost IPC using a per-session credential, exact process/session identity, protocol version, and challenge. Do not expose the normal project bridge token or accept arbitrary peers.
- Extend the editor session coordinator to launch only Unreal-owned PIE processes, authenticate companions, aggregate instance state and attributed diagnostics, route bounded inspect/command/wait requests, and detect disconnects or crashes.
- Apply the Phase 33 reflection and actor-handle policy and the Phase 34 test-command policy independently inside each child. Keep all payloads, queues, scans, properties, logs, waits, and retained state bounded.
- Publish supported process modes, topologies, player/client limits, companion availability, protocol version, and degraded/failed-instance state through capabilities and session inspection.
- Clean partial launches and every owned child on startup failure, cancellation, stop, bridge shutdown, or editor exit. Detect leaked child processes without exposing forced termination as a general model operation.
- Preserve source portability with narrow macOS, Windows, and Linux IPC/process adapters and no new runtime dependency.

### Verification

- Test credential and version mismatch, spoofed and duplicate companions, foreign processes, disconnect, crash, timeout, cancellation, partial launch, queue and payload limits, replay, per-process logs, cleanup, and leaked-process reporting.
- Run two-player listen-server and dedicated-server plus two-client sessions in multi-process mode on macOS and Windows. Verify actor inspection, properties, roles, test commands, waits, and attributed results across every process.
- Prove no child accepts arbitrary network clients, console commands, UObject calls, filesystem access, process control, supplied code, or commands outside the retained editor-owned session.

### Documentation and completion gate

- Document companion packaging, authentication, IPC, process/session identities, supported topologies, failure and cleanup behavior, security boundaries, platform behavior, and capability negotiation.
- Extend the session examples with multi-process listen-server and dedicated-server-plus-two-client variants using the same bounded inspection and test contracts.
- Complete the phase only when multi-process start, observation, testing, and stop satisfy the same bounded contracts as single-process PIE without introducing a general game-side control surface.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
