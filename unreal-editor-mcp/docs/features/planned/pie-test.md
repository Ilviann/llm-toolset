---
feature_id: pie-test
status: planned
depends_on:
  - spline-edit
  - pie-inspect
released_in: null
---

# `pie-test` — Bounded PIE test commands, waits, and multiplayer acceptance

**Outcome:** Agents can run allowlisted test actions and bounded waits in exact PIE instances and verify a reusable representative gameplay scenario in single-process multiplayer PIE.

**Depends on:**

- [`spline-edit`](spline-edit.md)
- [`pie-inspect`](pie-inspect.md)

### Implementation

- Add `play_session_command` with exact discriminated operations for allowlisted console commands, named Automation tests, exact Functional Test actors, explicitly test-callable reflected functions, and bounded wait predicates.
- Publish the console allowlist and reject arguments, command chaining, aliases, file/process/network commands, arbitrary travel, and every unlisted command. Keep the default allowlist empty outside test configuration.
- Separate editor-wide Automation tests from world-specific Functional Tests in the schema and results. Do not claim an Automation test executed inside a selected runtime world.
- Permit reflected calls only on an exact runtime actor in an exact instance when the function carries plugin-owned test metadata, has a supported bounded signature, and passes a configured allowlist. Do not expose arbitrary `ProcessEvent`.
- Implement asynchronous waits for reflected property predicates, actor spawn/destruction, connection transitions, and test completion with explicit deadlines, polling/event bounds, cancellation, and terminal evidence.
- Return structured test results and bounded attributed events rather than parsing unrestricted console text as authority.
- Add disposable native/Blueprint/Functional Test fixtures for authoritative spawn, movement checkpoints, replicated state transitions, periodic server-side interaction, scenario completion, host action, and remote-client action.
- Run the acceptance against a disposable authored test map containing a spawn region, traversal spline, interaction target, and scenario controller; use one listen-server/host world and one remote-client world.

### Verification

- Test every allowed and rejected command shape, allowlist configuration, function metadata/signatures, wrong actor/world/session, Automation and Functional Test results, predicate types, spawn/destruction waits, timeout, cancellation, replay, log attribution, and result bounds.
- Prove console input cannot escape the allowlist and reflection cannot invoke unmarked functions or traverse arbitrary objects.
- Run the complete single-process flow: authority-only spawn, replication to host and remote client, spline traversal, server-authoritative state changes, host and remote-client action paths, replicated state transitions, independent scenario completion, and clean stop.

### Documentation and completion gate

- Document command/test policies, allowlist configuration, reflected test metadata, wait predicates, structured results, attribution, cancellation, security boundaries, and the corrected two-world listen-server acceptance.
- Add a complete bounded project-neutral example covering level placement, spline authoring, session lifecycle, runtime inspection, waits, test actions, and final cleanup.
- Complete the feature only when the representative multiplayer flow can be authored and verified end to end through MCP plus normal source/build tools in single-process PIE.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
