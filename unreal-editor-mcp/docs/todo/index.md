# Roadmap phase details

This directory contains the detailed implementation, verification, documentation, and completion requirements for every roadmap phase. [`ROADMAP.md`](../../ROADMAP.md) remains the concise phase checklist.

## Roadmap workflow

The target is Unreal Engine 5.8 and newer. macOS remains the primary development host. Native Windows validation begins with the Actor beta and becomes a release gate for every later feature phase. Linux remains source-portable with unit-tested platform branches until a native host is available.

Keep the authoritative checklist in [`ROADMAP.md`](../../ROADMAP.md) synchronized one-for-one with the phase documents below. Complete phases in order unless a phase explicitly permits parallel platform validation. Every phase must include implementation, tests, documentation, examples, and a releasable completion gate.

## Phase documents

- [Phase 4 — Reliable mutations, Actor components, and defaults](phase-04.md) — Add reliable mutation delivery, Actor component editing, and Blueprint/component defaults.
- [Phase 5 — Blueprint member variables](phase-05.md) — Add typed Blueprint member-variable inspection and editing.
- [Phase 6 — Function signatures and local variables](phase-06.md) — Add function signatures, function shells, and local variables.
- [Phase 7 — Macros and custom events](phase-07.md) — Add macro and custom-event shells with matching inspection.
- [Phase 8 — Action-catalog infrastructure and core actions](phase-08.md) — Add the bounded action-catalog infrastructure and core actions.
- [Phase 9 — Expanded action-catalog families](phase-09.md) — Expand the action catalog to the remaining supported action families.
- [Phase 10 — Graph-node lifecycle](phase-10.md) — Add transactional graph-node creation, movement, and removal.
- [Phase 11 — Pin defaults and direct connections](phase-11.md) — Add pin defaults and direct graph connections without automatic conversion.
- [Phase 12 — Wildcards, conversions, and complete atomic graph editing](phase-12.md) — Add wildcard specialization, explicit conversion insertion, and complete atomic graph editing.
- [Phase 13 — Actor workflow hardening on macOS](phase-13.md) — Harden the complete Actor workflow natively on macOS.
- [Phase 14 — Native Windows Actor beta](phase-14.md) — Qualify and publish the Actor Blueprint beta on native Windows.
- [Phase 15 — GameMode and GameState families](phase-15.md) — Formalize GameMode and GameState family support.
- [Phase 16 — GameInstance family](phase-16.md) — Add GameInstance family support.
- [Phase 17 — Complete function replacement](phase-17.md) — Add transactional replacement of one complete user-owned function.
- [Phase 18 — Event, custom-event, and macro replacement](phase-18.md) — Extend bounded replacement to events, custom events, and macros.
- [Phase 19 — Deterministic changed-node layout](phase-19.md) — Add deterministic layout for changed nodes.
- [Phase 20 — Cross-platform qualification and stable release](phase-20.md) — Complete cross-platform qualification and publish the first stable-tagged release.
- [Phase 21 — Optional configured editor launch](phase-21.md) — Add optional configured editor launch.
- [Phase 22 — Optional graceful editor shutdown](phase-22.md) — Add optional graceful editor shutdown.
- [Phase 23 — Optional durable editor restart](phase-23.md) — Add optional durable editor restart.
- [Phase 24 — Optional editor-offline project-file generation](phase-24.md) — Add optional editor-offline project-file generation.
- [Phase 25 — Optional editor-target builds](phase-25.md) — Add optional editor-target builds.

## Shared roadmap contracts

### Process boundary

The application remains an exact-version pair:

1. A dependency-free Python 3.10+ MCP server using stdio JSON-RPC.
2. An Unreal Editor C++ plugin using public editor APIs and a bounded authenticated localhost HTTP bridge.

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
| `blueprint_graph_edit` | 10 | Perform one typed node, pin, connection, position, or removal mutation |
| `blueprint_block_replace` | 17 | Replace one complete bounded logic unit as a prevalidated transaction |
| `editor_lifecycle` | 21 | Run one opt-in configured launch, restart, or graceful-shutdown operation |
| `project_build` | 24 | Run one opt-in configured project-generation or editor-target build operation |

Lifecycle and build tools remain absent from the default model context. Use an opt-in large mode for them. Measure the Blueprint schemas and use nested operation discriminators if context cost becomes excessive; the default mode must still support the complete Blueprint-authoring workflow.

### Mutation delivery and concurrency contracts

- Require a caller-generated `operation_id` for every mutating call, including existing mutation tools. Bind it to the exact normalized arguments, project identity, bridge instance, and authenticated client context.
- Retain a bounded operation ledger with published capacity and lifetime limits. Repeating an operation ID with the same request returns the retained result; reusing it with different arguments returns a stable conflict and never executes.
- Publish explicit operation states such as `queued`, `executing`, `committed`, `rejected`, and `outcome_unknown`. Never report cancellation after a mutation has committed.
- Cancellation may remove queued work or stop preflight work, but it must not interrupt an active Unreal mutation at an unsafe point. A lost response must be reconciled through `operation_status` before retry.
- The ledger is process-scoped unless a later operation explicitly defines durable restart state. If the bridge instance changes and no result is available, return `outcome_unknown` and require inspection before further mutation.
- Reject mutation while the target Blueprint is compiling, saving, loading, being reinstanced, undergoing undo/redo, or otherwise unable to provide stable preconditions.
- Use one editor transaction per accepted atomic mutation. Prevalidate before opening it, verify postconditions before commit, and implement explicit restoration for unexpected failure. Do not assume that cancelling a transaction restores arbitrary object state.

### Blueprint identity, type, and property contracts

- Use Unreal long package names and object/class paths at the model boundary; reject raw filesystem paths and traversal.
- Read-only operations may inspect any content mount visible to the project. The Blueprint being mutated must remain confined to `/Game` or a content mount owned by a plugin physically inside the current project's local `Plugins/` directory.
- A referenced class or asset is not itself a mutation target. Permit type-compatible native classes and packageable assets from visible mounted content while rejecting transient, editor-only, unresolved, incompatible, or unsafe references.
- Give components, members, graphs, nodes, pins, inspection snapshots, and bridge instances explicit identities. Mutation targets must have stable identities; an unavailable identity is not silently replaced by a name lookup.
- Require the current structural snapshot and all relevant object identities for mutation. Return `stale_precondition` instead of retargeting reconstructed or replacement objects.
- Reuse one bounded canonical K2 type and reflected-property codec across inspection and mutation. Add a type or value form only with read/write round-trip tests and explicit unsupported behavior.
- Validate every MCP argument against its published schema in Python and again against the live Unreal object, graph schema, property metadata, and family capabilities in C++.
- Use one stable bounded error envelope with `code`, `message`, `details`, and `retryable`. Never return C++ exceptions, assertions, addresses, credentials, or unbounded logs.
- Keep request bodies, JSON depth, strings, collections, scans, caches, operation state, diagnostics, response bytes, transaction work, and Game-thread time explicitly bounded and published through `capabilities`.

### Security baseline

- Bind only to `127.0.0.1` and verify the actual listening address in native integration tests.
- Authenticate every request with the high-entropy per-project credential and fail closed on credential, listener, route, or heartbeat faults.
- Never expose the credential or absolute project path through discovery, capabilities, operation records, diagnostics, or logs.
- Permit one bridge owner per configured port; bound queued requests and retained state; and cleanly release route, discovery, credentials, and pending work during shutdown.
- Never expose arbitrary UObject calls, unrestricted reflection mutation, Python execution inside Unreal, console commands, supplied C++, arbitrary subprocess arguments, or general filesystem/process access.

### Release discipline

Increment the minor version after each completed phase and the patch version for fixes. A major-version promotion requires a separate explicit decision. Keep Python metadata, `.uplugin` metadata, runtime capabilities, tests, README, examples, and `HISTORY.md` synchronized after every phase. Phase sections do not repeat version-update tasks.

## Deferred and excluded scope

The following are not part of the committed remaining roadmap unless separately authorized:

- Arbitrary selected-region block replacement beyond the complete logic-unit boundaries supported in Phases 17 and 18.
- General filesystem access or C++ source modification.
- Arbitrary shell commands, compiler arguments, console commands, UObject calls, unrestricted reflection mutation, expressions, or supplied-code evaluation.
- Unrestricted whole-Blueprint text import/export or wholesale Blueprint replacement.
- Blueprint reparenting, project-settings mutation, timelines, event dispatchers, interface authoring, and specialized asset families unless added through a later roadmap update.
- Level Blueprint, Widget Blueprint, Animation Blueprint, Control Rig, Niagara, Material, Behavior Tree, or StateTree authoring.
- Play-in-Editor input injection, screenshots, runtime object mutation, or automated gameplay assertions.
- Cloud services, accounts, telemetry, dependency downloads, or a game-side network listener.

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
