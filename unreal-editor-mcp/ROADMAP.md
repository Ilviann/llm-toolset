# Unreal Editor MCP Roadmap

The remaining order preserves the existing Blueprint, UI, lifecycle, and build phases, then adds a bounded level-authoring and multiplayer PIE track. The appended track delivers the single-process Canyon workflow before introducing the optional multi-process runtime companion.

- [x] [Phase 4 — Reliable mutations, Actor components, and defaults](docs/todo/phase-04.md) — Add reliable mutation delivery, Actor component editing, and Blueprint/component defaults.
- [x] [Phase 5 — Blueprint member variables](docs/todo/phase-05.md) — Add typed Blueprint member-variable inspection and editing.
- [x] [Phase 6 — Function signatures and local variables](docs/todo/phase-06.md) — Add function signatures, function shells, and local variables.
- [x] [Phase 7 — Macros and custom events](docs/todo/phase-07.md) — Add macro and custom-event shells with matching inspection.
- [x] [Phase 8 — Action-catalog infrastructure and core actions](docs/todo/phase-08.md) — Add the bounded action-catalog infrastructure and core actions.
- [x] [Phase 9 — C++ architecture and test decomposition](docs/todo/phase-09.md) — Split oversized native components and Automation Tests along cohesive internal boundaries without changing behavior.
- [x] [Phase 10 — Expanded action-catalog families](docs/todo/phase-10.md) — Expand the action catalog to the remaining supported action families.
- [x] [Phase 11 — Graph-node lifecycle](docs/todo/phase-11.md) — Add transactional graph-node creation, movement, and removal.
- [x] [Phase 12 — Pin defaults and direct connections](docs/todo/phase-12.md) — Add pin defaults and direct graph connections without automatic conversion.
- [x] [Phase 13 — Wildcards, conversions, and complete atomic graph editing](docs/todo/phase-13.md) — Add wildcard specialization, explicit conversion insertion, and complete atomic graph editing.
- [x] [Phase 14 — GameMode and GameState families](docs/todo/phase-14.md) — Formalize GameMode and GameState family support.
- [x] [Phase 15 — GameInstance family](docs/todo/phase-15.md) — Add GameInstance family support.
- [x] [Phase 16 — Multiplayer Blueprint authoring and framework assignment](docs/todo/phase-16.md) — Add RPC custom events, replication settings, and narrow GameMode/GameInstance project assignment.
- [x] [Phase 17 — User-defined structs and Data Tables](docs/todo/phase-17.md) — Add bounded row-schema and typed game-design table authoring.
- [ ] [Phase 18 — Widget Blueprint family and widget trees](docs/todo/phase-18.md) — Add Widget Blueprint creation, inspection, compilation, saving, and widget-tree editing.
- [ ] [Phase 19 — UMG layout, styling, bindings, and UI logic](docs/todo/phase-19.md) — Complete practical HUD and menu authoring on the Widget Blueprint family.
- [ ] [Phase 20 — Complete function replacement](docs/todo/phase-20.md) — Add transactional replacement of one complete user-owned function.
- [ ] [Phase 21 — Event, custom-event, and macro replacement](docs/todo/phase-21.md) — Extend bounded replacement to events, custom events, and macros.
- [ ] [Phase 22 — Deterministic changed-node layout](docs/todo/phase-22.md) — Add deterministic layout for changed nodes.
- [ ] [Phase 23 — Optional configured editor launch](docs/todo/phase-23.md) — Add optional configured editor launch.
- [ ] [Phase 24 — Optional graceful editor shutdown](docs/todo/phase-24.md) — Add optional graceful editor shutdown.
- [ ] [Phase 25 — Optional durable editor restart](docs/todo/phase-25.md) — Add optional durable editor restart.
- [ ] [Phase 26 — Optional editor-offline project-file generation](docs/todo/phase-26.md) — Add optional editor-offline project-file generation.
- [ ] [Phase 27 — Optional editor-target builds](docs/todo/phase-27.md) — Add optional editor-target builds.
- [ ] [Phase 28 — Level discovery, safe opening, and snapshot foundations](docs/todo/phase-28.md) — Add bounded map discovery, explicit safe map opening, and restart-stable level snapshots.
- [ ] [Phase 29 — World Partition actor and instance inspection](docs/todo/phase-29.md) — Inspect bounded descriptor, actor, component, and reflected instance state without loading the entire world.
- [ ] [Phase 30 — Transactional level actor editing and verified saving](docs/todo/phase-30.md) — Add stale-safe actor batches and honest per-package World Partition save verification.
- [ ] [Phase 31 — Spline component inspection and editing](docs/todo/phase-31.md) — Add bounded mixed-point spline inspection, mutation, persistence, and metadata safety.
- [ ] [Phase 32 — Retained operations and single-process multiplayer PIE lifecycle](docs/todo/phase-32.md) — Start and stop observable single-process PIE sessions, including a listen server and remote client.
- [ ] [Phase 33 — Per-world runtime actor inspection and attributed diagnostics](docs/todo/phase-33.md) — Inspect exact server/client worlds with session-scoped actor identities and proven log attribution.
- [ ] [Phase 34 — Bounded PIE test commands, waits, and Canyon acceptance](docs/todo/phase-34.md) — Add allowlisted test actions and complete the single-process Canyon Infantry acceptance flow.
- [ ] [Phase 35 — Multi-process PIE companion and cross-process observation](docs/todo/phase-35.md) — Extend retained sessions through an authenticated local runtime companion for owned PIE processes.
