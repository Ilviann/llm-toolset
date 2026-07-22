# Phase 16 — Multiplayer Blueprint authoring and framework assignment

**Outcome:** Agents can author the replication and RPC contracts used by a multiplayer shooter and can activate the project's default GameMode and GameInstance through a narrow, observable settings operation.

### Implementation

- Extend custom-event metadata and inspection with live Unreal RPC semantics: not replicated, server, owning client, or multicast, plus reliability where the selected mode permits it. Preserve the custom-event node identity and reject unsupported signatures, family/graph contexts, conflicting flags, and forged function metadata before mutation.
- Add typed Actor and ActorComponent replication settings through the existing default/component mutation paths, including the live-supported equivalents of actor replication, movement replication, relevancy, dormancy, priority/update frequency, and component replication. Do not expose unrestricted reflection.
- Retain and exercise the released replicated-variable and RepNotify contracts, including lifetime conditions and notification-function coupling. Publish exact per-family multiplayer capabilities rather than implying every setting is valid on every Actor-derived class.
- Add `gameplay_framework_edit` for only the configured project's default GameMode and GameInstance class assignments. Resolve exact compatible saved Blueprint-generated or native classes, report the old/new setting and restart requirement, and keep world-specific overrides, arbitrary config keys, config paths, and general Project Settings mutation unavailable.
- Reuse operation IDs, stale preconditions, exact project identity, bounded diagnostics, atomic config persistence, read-back verification, and lost-response reconciliation. A rejected or failed assignment must preserve the prior on-disk setting.
- Keep runtime server control, client input injection, gameplay cheats, console commands, and arbitrary PIE operations outside the model-facing surface.

### Verification

- Create and read back representative replicated Character/Pawn/PlayerState components, replicated and RepNotify variables, and reliable/unreliable server, client, and multicast custom events.
- Test invalid RPC parameters, incompatible family/graph contexts, conflicting reliability/mode combinations, component and actor replication dependencies, stale snapshots, undo/redo, compile/save/reload, timeout, replay, and lost-response recovery.
- Assign and restore compatible native and Blueprint default GameMode/GameInstance classes; test missing, unsaved, wrong-family, stale, read-only, source-controlled, and write-failure cases without accepting arbitrary config mutation.
- Add internal automation-only multiplayer behavioral coverage proving authored RPC/function flags, replication descriptors, notification relationships, and framework settings behave as represented. Do not add model-facing runtime-control tools.
- Run the complete suite natively on macOS and Windows and require identical model-facing contracts.

### Documentation and completion gate

- Document authority/ownership implications, RPC delivery limitations, replication settings, RepNotify coupling, framework assignment, restart behavior, manual world overrides, and focused multiplayer-shooter examples.
- Complete the phase only when the supported multiplayer contracts survive compile/save/restart, settings failures restore exactly, and automation verifies the represented networking semantics on both native platforms.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
