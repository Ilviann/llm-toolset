# Unreal Editor MCP Roadmap

Feature identifiers are stable names, not execution indexes. Unfinished features may be implemented and completed in any order once every direct dependency in their description is complete. The checklist retains a completed feature only while an unfinished feature directly depends on it; completed feature documents and historical `phase-*` identifiers remain available under [`docs/features/`](docs/features/index.md).

- [x] [`phase-13` — Wildcards, conversions, and complete atomic graph editing](docs/features/completed/phase-13.md) — Add wildcard specialization, explicit conversion insertion, and complete atomic graph editing.
- [x] [`umg-authoring` — UMG layout, styling, bindings, and UI logic](docs/features/completed/umg-authoring.md) — Complete practical HUD and menu authoring on the Widget Blueprint family.
  - Depends on:
    - `widget-tree`
- [ ] [`umg-mvvm` — UMG ViewModels and View Bindings](docs/features/planned/umg-mvvm.md) — Add typed MVVM ViewModel and Widget View Binding authoring through an optional lockstep-versioned companion plugin.
  - Depends on:
    - `umg-authoring`
- [ ] [`gas-ability-blueprints-inspect` — Gameplay Ability Blueprint inspection](docs/features/planned/gas-ability-blueprints-inspect.md) — Add bounded typed inspection of existing Gameplay Ability Blueprint assets through an optional lockstep-versioned companion plugin.
- [ ] [`gas-ability-blueprints` — Gameplay Ability Blueprint creation and updating](docs/features/planned/gas-ability-blueprints.md) — Add typed Gameplay Ability Blueprint creation and authoring through the GAS companion.
  - Depends on:
    - `phase-13`
    - `gas-ability-blueprints-inspect`
- [ ] [`gas-gameplay-effects-inspect` — Gameplay Effect inspection](docs/features/planned/gas-gameplay-effects-inspect.md) — Add bounded typed inspection of existing data-only Gameplay Effect Blueprint assets through the GAS companion.
  - Depends on:
    - `gas-ability-blueprints-inspect`
- [ ] [`gas-gameplay-effects` — Gameplay Effect creation and updating](docs/features/planned/gas-gameplay-effects.md) — Add typed creation and data-only updating of Gameplay Effect Blueprint assets through the GAS companion.
  - Depends on:
    - `gas-gameplay-effects-inspect`
- [x] [`function-replace` — Complete function replacement](docs/features/completed/function-replace.md) — Add transactional replacement of one complete user-owned function.
- [ ] [`event-macro-replace` — Event, custom-event, and macro replacement](docs/features/planned/event-macro-replace.md) — Extend bounded replacement to events, custom events, and macros.
  - Depends on:
    - `function-replace`
- [ ] [`node-layout` — Deterministic changed-node layout](docs/features/planned/node-layout.md) — Add deterministic layout for changed nodes.
  - Depends on:
    - `event-macro-replace`
- [ ] [`pcg-graph-authoring` — Procedural Content Generation graph authoring](docs/features/planned/pcg-graph-authoring.md) — Discover, inspect, create, and transactionally edit bounded PCG Graph assets.
- [x] [`editor-restart` — Optional durable editor restart](docs/features/completed/editor-restart.md) — Add optional durable editor restart.
  - Depends on:
    - `editor-launch`
    - `editor-shutdown`
- [ ] [`project-files` — Optional editor-offline project-file generation](docs/features/planned/project-files.md) — Add optional editor-offline project-file generation.
  - Depends on:
    - `editor-restart`
- [ ] [`editor-build` — Optional editor-target builds](docs/features/planned/editor-build.md) — Add optional editor-target builds.
  - Depends on:
    - `project-files`
- [x] [`level-open` — Level discovery, safe opening, and snapshot foundations](docs/features/completed/level-open.md) — Add bounded map discovery, explicit safe map opening, and restart-stable level snapshots.
  - Depends on:
    - [`issue-1` resolution](docs/issues/issue-1.md) — resolved in 0.17.1
- [x] [`level-inspect` — World Partition actor and instance inspection](docs/features/completed/level-inspect.md) — Inspect bounded descriptor, actor, component, and reflected instance state without loading the entire world.
  - Depends on:
    - `level-open`
- [x] [`level-edit` — Transactional level actor editing and verified saving](docs/features/completed/level-edit.md) — Add stale-safe actor batches and honest per-package World Partition save verification.
  - Depends on:
    - `level-inspect`
- [ ] [`pcg-component-edit` — Level actor PCG Component editing](docs/features/planned/pcg-component-edit.md) — Inspect, configure, generate, clean up, and persist PCG Components on exact level actors.
  - Depends on:
    - `pcg-graph-authoring`
    - `level-edit`
- [ ] [`spline-edit` — Spline component inspection and editing](docs/features/planned/spline-edit.md) — Add bounded mixed-point spline inspection, mutation, persistence, and metadata safety.
  - Depends on:
    - `level-edit`
- [ ] [`pie-lifecycle` — Retained operations and single-process multiplayer PIE lifecycle](docs/features/planned/pie-lifecycle.md) — Start and stop observable single-process PIE sessions, including a listen server and remote client.
  - Depends on:
    - `level-open`
- [ ] [`pie-inspect` — Per-world runtime actor inspection and attributed diagnostics](docs/features/planned/pie-inspect.md) — Inspect exact server/client worlds with session-scoped actor identities and proven log attribution.
  - Depends on:
    - `pie-lifecycle`
- [ ] [`pie-test` — Bounded PIE test commands, waits, and multiplayer acceptance](docs/features/planned/pie-test.md) — Add allowlisted test actions and complete a reusable single-process multiplayer acceptance flow.
  - Depends on:
    - `spline-edit`
    - `pie-inspect`
- [ ] [`pie-multiprocess` — Multi-process PIE companion and cross-process observation](docs/features/planned/pie-multiprocess.md) — Extend retained sessions through an authenticated local runtime companion for owned PIE processes.
  - Depends on:
    - `pie-test`

## Native platform test backlog

Feature checkboxes record implementation completion. This section separately lists completed features that have not yet passed their applicable native platform verification.

- macOS:
  - `level-management`
  - `level-edit`
- Windows:
  - None
- Linux:
  - None

Linux lifecycle launch and restart are intentionally unsupported, so the lifecycle features do not require native Linux verification.
