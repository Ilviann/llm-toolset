# Unreal Editor MCP Roadmap

Feature identifiers are stable names, not execution indexes. Unfinished features may be implemented and completed in any order once every direct dependency in their description is complete. Completed features retain their historical `phase-*` identifiers.

- [x] [`phase-4` — Reliable mutations, Actor components, and defaults](docs/todo/phase-4.md) — Add reliable mutation delivery, Actor component editing, and Blueprint/component defaults.
- [x] [`phase-5` — Blueprint member variables](docs/todo/phase-5.md) — Add typed Blueprint member-variable inspection and editing.
- [x] [`phase-6` — Function signatures and local variables](docs/todo/phase-6.md) — Add function signatures, function shells, and local variables.
- [x] [`phase-7` — Macros and custom events](docs/todo/phase-7.md) — Add macro and custom-event shells with matching inspection.
- [x] [`phase-8` — Action-catalog infrastructure and core actions](docs/todo/phase-8.md) — Add the bounded action-catalog infrastructure and core actions.
- [x] [`phase-9` — C++ architecture and test decomposition](docs/todo/phase-9.md) — Split oversized native components and Automation Tests along cohesive internal boundaries without changing behavior.
- [x] [`phase-10` — Expanded action-catalog families](docs/todo/phase-10.md) — Expand the action catalog to the remaining supported action families.
- [x] [`phase-11` — Graph-node lifecycle](docs/todo/phase-11.md) — Add transactional graph-node creation, movement, and removal.
- [x] [`phase-12` — Pin defaults and direct connections](docs/todo/phase-12.md) — Add pin defaults and direct graph connections without automatic conversion.
- [x] [`phase-13` — Wildcards, conversions, and complete atomic graph editing](docs/todo/phase-13.md) — Add wildcard specialization, explicit conversion insertion, and complete atomic graph editing.
- [x] [`phase-14` — GameMode and GameState families](docs/todo/phase-14.md) — Formalize GameMode and GameState family support.
- [x] [`phase-15` — GameInstance family](docs/todo/phase-15.md) — Add GameInstance family support.
- [x] [`phase-16` — Multiplayer Blueprint authoring and framework assignment](docs/todo/phase-16.md) — Add RPC custom events, replication settings, and narrow GameMode/GameInstance project assignment.
- [x] [`phase-17` — User-defined structs and Data Tables](docs/todo/phase-17.md) — Add bounded row-schema and typed game-design table authoring.
- [ ] [`widget-tree` — Widget Blueprint family and widget trees](docs/todo/widget-tree.md) — Add Widget Blueprint creation, inspection, compilation, saving, and widget-tree editing.
- [ ] [`umg-authoring` — UMG layout, styling, bindings, and UI logic](docs/todo/umg-authoring.md) — Complete practical HUD and menu authoring on the Widget Blueprint family.
  - Depends on:
    - `widget-tree`
- [ ] [`function-replace` — Complete function replacement](docs/todo/function-replace.md) — Add transactional replacement of one complete user-owned function.
- [ ] [`event-macro-replace` — Event, custom-event, and macro replacement](docs/todo/event-macro-replace.md) — Extend bounded replacement to events, custom events, and macros.
  - Depends on:
    - `function-replace`
- [ ] [`node-layout` — Deterministic changed-node layout](docs/todo/node-layout.md) — Add deterministic layout for changed nodes.
  - Depends on:
    - `event-macro-replace`
- [x] [`editor-launch` — Optional configured editor launch](docs/todo/editor-launch.md) — Add optional configured editor launch.
- [x] [`editor-shutdown` — Optional graceful editor shutdown](docs/todo/editor-shutdown.md) — Add optional graceful editor shutdown.
  - Depends on:
    - `editor-launch`
- [x] [`editor-restart` — Optional durable editor restart](docs/todo/editor-restart.md) — Add optional durable editor restart.
  - Depends on:
    - `editor-launch`
    - `editor-shutdown`
- [ ] [`project-files` — Optional editor-offline project-file generation](docs/todo/project-files.md) — Add optional editor-offline project-file generation.
  - Depends on:
    - `editor-restart`
- [ ] [`editor-build` — Optional editor-target builds](docs/todo/editor-build.md) — Add optional editor-target builds.
  - Depends on:
    - `project-files`
- [ ] [`level-open` — Level discovery, safe opening, and snapshot foundations](docs/todo/level-open.md) — Add bounded map discovery, explicit safe map opening, and restart-stable level snapshots.
  - Depends on:
    - [`issue-1` resolution](docs/issues/issue-1.md)
- [ ] [`level-inspect` — World Partition actor and instance inspection](docs/todo/level-inspect.md) — Inspect bounded descriptor, actor, component, and reflected instance state without loading the entire world.
  - Depends on:
    - `level-open`
- [ ] [`level-edit` — Transactional level actor editing and verified saving](docs/todo/level-edit.md) — Add stale-safe actor batches and honest per-package World Partition save verification.
  - Depends on:
    - `level-inspect`
- [ ] [`spline-edit` — Spline component inspection and editing](docs/todo/spline-edit.md) — Add bounded mixed-point spline inspection, mutation, persistence, and metadata safety.
  - Depends on:
    - `level-edit`
- [ ] [`pie-lifecycle` — Retained operations and single-process multiplayer PIE lifecycle](docs/todo/pie-lifecycle.md) — Start and stop observable single-process PIE sessions, including a listen server and remote client.
  - Depends on:
    - `level-open`
- [ ] [`pie-inspect` — Per-world runtime actor inspection and attributed diagnostics](docs/todo/pie-inspect.md) — Inspect exact server/client worlds with session-scoped actor identities and proven log attribution.
  - Depends on:
    - `pie-lifecycle`
- [ ] [`pie-test` — Bounded PIE test commands, waits, and multiplayer acceptance](docs/todo/pie-test.md) — Add allowlisted test actions and complete a reusable single-process multiplayer acceptance flow.
  - Depends on:
    - `spline-edit`
    - `pie-inspect`
- [ ] [`pie-multiprocess` — Multi-process PIE companion and cross-process observation](docs/todo/pie-multiprocess.md) — Extend retained sessions through an authenticated local runtime companion for owned PIE processes.
  - Depends on:
    - `pie-test`

## Native platform test backlog

Feature checkboxes record implementation completion. This section separately lists completed features that have not yet passed their applicable native platform verification.

- macOS:
  - `editor-launch`
  - `editor-shutdown`
  - `editor-restart`
- Windows:
  - None
- Linux:
  - None

Linux lifecycle launch and restart are intentionally unsupported, so the lifecycle features do not require native Linux verification.
