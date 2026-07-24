# Roadmap phase details

This directory contains the detailed implementation, verification, documentation, and completion requirements for every roadmap phase. [`ROADMAP.md`](../../ROADMAP.md) remains the concise phase checklist.

## Roadmap workflow

The target is Unreal Engine 5.8 and newer. macOS remains the primary development host. Platform validation belongs to each applicable feature phase rather than separate roadmap phases. Linux remains source-portable with unit-tested platform branches until a native host is available.

Keep the authoritative checklist in [`ROADMAP.md`](../../ROADMAP.md) synchronized one-for-one with the phase documents below. Complete phases in order unless a phase explicitly permits parallel platform validation. Every phase must include implementation, tests, documentation, examples, and a releasable completion gate.

## Phase documents

- [Phase 4 — Reliable mutations, Actor components, and defaults](phase-04.md) — Add reliable mutation delivery, Actor component editing, and Blueprint/component defaults.
- [Phase 5 — Blueprint member variables](phase-05.md) — Add typed Blueprint member-variable inspection and editing.
- [Phase 6 — Function signatures and local variables](phase-06.md) — Add function signatures, function shells, and local variables.
- [Phase 7 — Macros and custom events](phase-07.md) — Add macro and custom-event shells with matching inspection.
- [Phase 8 — Action-catalog infrastructure and core actions](phase-08.md) — Add the bounded action-catalog infrastructure and core actions.
- [Phase 9 — C++ architecture and test decomposition](phase-09.md) — Split oversized native components and Automation Tests along cohesive internal boundaries without changing behavior.
- [Phase 10 — Expanded action-catalog families](phase-10.md) — Expand the action catalog to the remaining supported action families.
- [Phase 11 — Graph-node lifecycle](phase-11.md) — Add transactional graph-node creation, movement, and removal.
- [Phase 12 — Pin defaults and direct connections](phase-12.md) — Add pin defaults and direct graph connections without automatic conversion.
- [Phase 13 — Wildcards, conversions, and complete atomic graph editing](phase-13.md) — Add wildcard specialization, explicit conversion insertion, and complete atomic graph editing.
- [Phase 14 — GameMode and GameState families](phase-14.md) — Formalize GameMode and GameState family support.
- [Phase 15 — GameInstance family](phase-15.md) — Add GameInstance family support.
- [Phase 16 — Multiplayer Blueprint authoring and framework assignment](phase-16.md) — Add RPC custom events, replication settings, and narrow GameMode/GameInstance project assignment.
- [Phase 17 — User-defined structs and Data Tables](phase-17.md) — Add bounded row-schema and typed game-design table authoring.
- [Phase 18 — Widget Blueprint family and widget trees](phase-18.md) — Add Widget Blueprint creation, inspection, compilation, saving, and widget-tree editing.
- [Phase 19 — UMG layout, styling, bindings, and UI logic](phase-19.md) — Complete practical HUD and menu authoring on the Widget Blueprint family.
- [Phase 20 — Complete function replacement](phase-20.md) — Add transactional replacement of one complete user-owned function.
- [Phase 21 — Event, custom-event, and macro replacement](phase-21.md) — Extend bounded replacement to events, custom events, and macros.
- [Phase 22 — Deterministic changed-node layout](phase-22.md) — Add deterministic layout for changed nodes.
- [Phase 23 — Optional configured editor launch](phase-23.md) — Add optional configured editor launch.
- [Phase 24 — Optional graceful editor shutdown](phase-24.md) — Add optional graceful editor shutdown.
- [Phase 25 — Optional durable editor restart](phase-25.md) — Add optional durable editor restart.
- [Phase 26 — Optional editor-offline project-file generation](phase-26.md) — Add optional editor-offline project-file generation.
- [Phase 27 — Optional editor-target builds](phase-27.md) — Add optional editor-target builds.
- [Phase 28 — Level discovery, safe opening, and snapshot foundations](phase-28.md) — Add bounded map discovery, explicit safe map opening, and restart-stable level snapshots.
- [Phase 29 — World Partition actor and instance inspection](phase-29.md) — Inspect bounded descriptor, actor, component, and reflected instance state without loading the entire world.
- [Phase 30 — Transactional level actor editing and verified saving](phase-30.md) — Add stale-safe actor batches and honest per-package World Partition save verification.
- [Phase 31 — Spline component inspection and editing](phase-31.md) — Add bounded mixed-point spline inspection, mutation, persistence, and metadata safety.
- [Phase 32 — Retained operations and single-process multiplayer PIE lifecycle](phase-32.md) — Start and stop observable single-process PIE sessions, including a listen server and remote client.
- [Phase 33 — Per-world runtime actor inspection and attributed diagnostics](phase-33.md) — Inspect exact server/client worlds with session-scoped actor identities and proven log attribution.
- [Phase 34 — Bounded PIE test commands, waits, and Canyon acceptance](phase-34.md) — Add allowlisted test actions and complete the single-process Canyon Infantry acceptance flow.
- [Phase 35 — Multi-process PIE companion and cross-process observation](phase-35.md) — Extend retained sessions through an authenticated local runtime companion for owned PIE processes.

## Shared roadmap contracts

### Process boundary

Through Phase 34, the application remains an exact-version pair:

1. A dependency-free Python 3.10+ MCP server using stdio JSON-RPC.
2. An Unreal Editor C++ plugin using public editor APIs and a bounded authenticated localhost HTTP bridge.

Phase 35 adds a minimal exact-version runtime companion module to the same plugin distribution for editor-owned multi-process PIE children. It connects outward to editor-owned authenticated IPC and does not expose a model-facing game listener.

Python owns MCP framing, published schemas, exact argument validation, tool modes, project configuration, discovery, authenticated HTTP calls, timeouts, process orchestration, and stable error presentation. The C++ plugin owns credentials, listener lifecycle, authentication, Game-thread dispatch, Unreal object access, Blueprint operations, compiler diagnostics, transactions, package saving, mutation-result retention, and authoritative capabilities.

Deploy the Python package and C++ plugin as an exact-version pair. Report both versions and the installed Unreal version through `capabilities`; reject a mismatched pair before mutation. Support for later Unreal releases must be proven through compilation and behavioral tests, not inferred from version numbers.

### Remaining model-facing tool surface

Keep the public surface compact. Add typed operations to these remaining tool families rather than publishing a separate tool for every native handler:

| Tool | First phase | Responsibility |
| --- | ---: | --- |
| `operation_status` | 4 | Resolve the retained outcome of one mutation operation without executing it again |
| `blueprint_component_edit` | 4 | Perform one typed component-hierarchy or component-default mutation |
| `blueprint_default_edit` | 4 | Set one supported Blueprint class-default property |
| `blueprint_member_edit` | 5 | Perform one typed variable, function, macro, or custom-event mutation |
| `blueprint_action_catalog` | 8 | Discover a bounded set of context-valid graph actions without mutation |
| `blueprint_graph_edit` | 11 | Perform one typed node, pin, connection, position, or removal mutation |
| `gameplay_framework_edit` | 16 | Assign only the configured project's default GameMode or GameInstance class |
| `game_data_inspect` | 17 | Inspect one bounded user-defined struct or Data Table schema/row page |
| `game_data_edit` | 17 | Create or mutate one bounded user-defined struct or Data Table transaction |
| `widget_tree_edit` | 18 | Perform one typed Widget Blueprint tree, widget-default, slot, layout, style, or binding mutation |
| `blueprint_block_replace` | 20 | Replace one complete bounded logic unit as a prevalidated transaction |
| `editor_lifecycle` | 23 | Run one opt-in configured launch, restart, or graceful-shutdown operation |
| `project_build` | 26 | Run one opt-in configured project-generation or editor-target build operation |
| `level_inspect` | 28 | Discover mounted maps and inspect bounded current-map, actor, component, property, and spline snapshot pages |
| `level_open` | 28 | Safely open one exact mounted map without implicit save or discard |
| `level_actor_edit` | 30 | Apply one stale-safe bounded actor/component/spline mutation batch in the current map |
| `level_save` | 30 | Save and verify the current map and explicit affected external-actor packages |
| `play_session_start` | 32 | Start one retained bounded PIE topology with exact effective settings and instance identities |
| `play_session_stop` | 32 | Idempotently stop one retained PIE session and report cleanup |
| `play_session_inspect` | 33 | Inspect one retained session or exact runtime world with bounded actor/property/log pages |
| `play_session_command` | 34 | Run one allowlisted test action or bounded wait against an exact retained session instance |

Lifecycle and build tools remain absent from the default model context. Use an opt-in large mode for them. Measure the Blueprint schemas and use nested operation discriminators if context cost becomes excessive; the default mode must still support the complete Blueprint-authoring workflow.

### Mutation delivery and concurrency contracts

- Require a caller-generated `operation_id` for every mutating call, including existing mutation tools. Bind it to the exact normalized arguments, project identity, bridge instance, and authenticated client context.
- Retain a bounded operation ledger with published capacity and lifetime limits. Repeating an operation ID with the same request returns the retained result; reusing it with different arguments returns a stable conflict and never executes.
- Publish explicit operation states such as `queued`, `executing`, `committed`, `rejected`, and `outcome_unknown`. Never report cancellation after a mutation has committed.
- Cancellation may remove queued work or stop preflight work, but it must not interrupt an active Unreal mutation at an unsafe point. A lost response must be reconciled through `operation_status` before retry.
- The ledger is process-scoped unless a later operation explicitly defines durable restart state. If the bridge instance changes and no result is available, return `outcome_unknown` and require inspection before further mutation.
- Retained PIE lifecycle and wait operations may remain nonterminal across HTTP requests. Publish bounded starting/running/stopping progress, allow cancellation only at safe points, and keep terminal replay semantics identical to short mutations.
- Reject mutation while the target asset is compiling, saving, loading, being reinstanced, undergoing undo/redo, or otherwise unable to provide stable preconditions.
- Use one editor transaction per accepted atomic asset mutation where Unreal supports it. Prevalidate before opening it, verify postconditions before commit, and implement explicit restoration for unexpected failure. Config-file operations must use atomic persistence and verified restoration instead of pretending to be editor transactions. Do not assume that cancelling a transaction restores arbitrary object state.

### Blueprint identity, type, and property contracts

- Use Unreal long package names and object/class paths at the model boundary; reject raw filesystem paths and traversal.
- Read-only operations may inspect any content mount visible to the project. An asset being mutated must remain confined to `/Game` or a content mount owned by a plugin physically inside the current project's local `Plugins/` directory.
- A referenced class or asset is not itself a mutation target. Permit type-compatible native classes and packageable assets from visible mounted content while rejecting transient, editor-only, unresolved, incompatible, or unsafe references.
- Give components, Blueprint members, struct members, table schemas/rows, widgets/slots, graphs, nodes, pins, inspection snapshots, and bridge instances explicit identities. Mutation targets must have stable identities; an unavailable identity is not silently replaced by a name lookup.
- Require the current structural snapshot and all relevant object identities for mutation. Return `stale_precondition` instead of retargeting reconstructed or replacement objects.
- Reuse one bounded canonical K2 type and reflected-property codec across inspection and mutation. Add a type or value form only with read/write round-trip tests and explicit unsupported behavior.
- Validate every MCP argument against its published schema in Python and again against the live Unreal object, graph schema, property metadata, and family capabilities in C++.
- Use one stable bounded error envelope with `code`, `message`, `details`, and `retryable`. Never return C++ exceptions, assertions, addresses, credentials, or unbounded logs.
- Keep request bodies, JSON depth, strings, collections, scans, caches, operation state, diagnostics, response bytes, transaction work, and Game-thread time explicitly bounded and published through `capabilities`.

### Level authoring contracts

- Keep `level_inspect` read-only and use the separate ledger-backed `level_open` operation for map switching. Never implicitly save, discard, or prompt for dirty work.
- Qualify stable Actor GUID and component identities by exact map identity. Require the current map snapshot and exact identities for every existing-object mutation.
- Use World Partition actor descriptors for bounded discovery and exact or region loading for live instance work. Missing or failed cells and data layers are errors, not evidence that an actor is absent.
- Prevalidate each complete actor/spline batch, transact where Unreal supports it, maintain explicit restoration for unexpected in-memory failure, and verify postconditions before reporting commit.
- Treat a multi-package save as a verified batch, not an atomic filesystem transaction. Return per-package outcomes and explicit partial failure; never claim that Unreal rolled back already persisted external packages.

### Retained PIE session contracts

- Give sessions, operations, instances, world contexts, runtime actors, logs, and tests explicit bounded identities. Require both session and instance/world identity for every world-specific query or action.
- A two-player listen server has a listen-server/host world and one remote-client world. Use a dedicated server plus two clients when three separate worlds are required.
- Publish supported topology, player/client, process-mode, inspection, command, wait, and test policies through `capabilities`; reject unsupported modes rather than falling back to mutable editor preferences.
- Single-process inspection remains editor-owned. Multi-process observation requires the Phase 35 exact-version companion and authenticated editor-owned IPC; never guess or scrape state from foreign child processes.
- Allow only configured console commands, named tests, plugin-marked reflected test functions, and bounded predicates. Do not expose arbitrary input injection, `ProcessEvent`, runtime reflection mutation, or unrestricted console execution.
- Return only diagnostics with a proven originating process/PIE instance. Exclude unattributable raw log entries instead of assigning them heuristically.

### Security baseline

- Bind only to `127.0.0.1` and verify the actual listening address in native integration tests.
- Authenticate every request with the high-entropy per-project credential and fail closed on credential, listener, route, or heartbeat faults.
- Never expose the credential or absolute project path through discovery, capabilities, operation records, diagnostics, or logs.
- Permit one bridge owner per configured port; bound queued requests and retained state; and cleanly release route, discovery, credentials, and pending work during shutdown.
- Never expose arbitrary UObject calls, unrestricted reflection mutation, Python execution inside Unreal, unrestricted console commands, supplied C++, arbitrary subprocess arguments, or general filesystem/process access. Retained PIE commands remain confined to the capability-advertised test allowlists and exact plugin-marked functions.

### Release discipline

Increment the minor version after each completed feature phase and the patch version for fixes or behavior-preserving refactoring phases. A major-version promotion requires a separate explicit decision. Keep Python metadata, `.uplugin` metadata, runtime capabilities, tests, README, examples, and `HISTORY.md` synchronized after every phase. Phase sections do not repeat version-update tasks.

## Deferred and excluded scope

The following are not part of the committed remaining roadmap unless separately authorized:

- Arbitrary selected-region block replacement beyond the complete logic-unit boundaries supported in Phases 20 and 21.
- General filesystem access or C++ source modification.
- Arbitrary shell commands, compiler arguments, console commands, UObject calls, unrestricted reflection mutation, expressions, or supplied-code evaluation.
- Unrestricted whole-Blueprint text import/export or wholesale Blueprint replacement.
- Blueprint reparenting, project-settings mutation beyond the narrow Phase 16 gameplay-framework assignments, timelines, event-dispatcher authoring, interface authoring, and specialized asset families not named in this roadmap.
- Level Blueprint, Animation Blueprint, Control Rig, Niagara, Material, Behavior Tree, StateTree, or Widget-animation authoring.
- General Play-in-Editor input injection, screenshots, runtime object mutation beyond exact configured test functions, arbitrary gameplay assertions, or unrestricted raw-log capture.
- Cloud services, accounts, telemetry, dependency downloads, or a model-facing game-side network listener.

## Primary Unreal 5.8 API references

These references establish feasibility only. Each owning phase must add compiled public-header probes and behavioral tests before freezing its model-facing contract:

- [FScopedTransaction](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/FScopedTransaction)
- [FAssetRegistryModule](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/AssetRegistry/FAssetRegistryModule)
- [FKismetEditorUtilities](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/FKismetEditorUtilities)
- [FBlueprintEditorUtils](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/FBlueprintEditorUtils)
- [USubobjectDataSubsystem](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/SubobjectDataInterface/USubobjectDataSubsystem)
- [UBlueprintNodeSpawner](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/BlueprintGraph/UBlueprintNodeSpawner)
- [UBlueprintNodeSpawner::Invoke](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/BlueprintGraph/UBlueprintNodeSpawner/Invoke)
- [UEdGraphSchema_K2](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/BlueprintGraph/UEdGraphSchema_K2)
- [FCompilerResultsLog](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/FCompilerResultsLog)
- [UPackage saving](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/CoreUObject/UPackage)
